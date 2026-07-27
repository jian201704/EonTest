#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QThread>
#include <memory>
#include <map>
#include <mutex>
#include <set>

#include "eon/sdk/IScpiIO.h"
#include "eon/infra/SerialScpiIO.h"
#include "eon/sdk/IStepPlugin.h"

namespace {

struct PowerConnection {
    std::unique_ptr<eon::infra::SerialScpiIO> io;
    std::mutex mutex;
    std::set<QString> workflows;
};

std::map<QString, std::unique_ptr<PowerConnection>> s_connections;
std::mutex s_connectionsMutex;

inline QString readPort(const QVariantMap& d, const QString& fallback) {
    return d.value("port", d.value("powerPort", fallback)).toString();
}
inline int readBaud(const QVariantMap& d, int fallback) {
    return d.value("baudRate", d.value("powerBaudRate", fallback)).toInt();
}
inline double readVoltage(const QVariantMap& d, double fallback) {
    return d.value("voltage", d.value("powerVoltage", fallback)).toDouble();
}
inline double readCurrent(const QVariantMap& d, double fallback) {
    return d.value("current", d.value("powerCurrent", fallback)).toDouble();
}
inline QString readAction(const QVariantMap& d, const QString& fallback) {
    QString action = d.value("action", d.value("powerAction", fallback)).toString().trimmed().toLower();
    if (action == "power_on" || action == "output_on") return "on";
    if (action == "power_off" || action == "output_off") return "off";
    return action;
}
inline int readDelay(const QVariantMap& d, int fallback) {
    return d.value("delay", d.value("powerOnDelayMs", fallback)).toInt();
}

QString connectionKey(const QVariantMap& data) {
    return QString("serial:%1:%2:%3:%4:%5")
        .arg(readPort(data, "COM3"))
        .arg(readBaud(data, 9600))
        .arg(data.value("dataBits", 8).toInt())
        .arg(data.value("parity", "N").toString().toUpper())
        .arg(data.value("stopBits", 1).toInt());
}

} // namespace

class PowerSupplyPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "powersupply.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override { return "power.supply"; }

    void postWorkflow(eon::sdk::WorkflowContext& context) override {
        const QString workflowKey = QString("%1/%2").arg(
            context.workflowId, context.data.value("_cellId", "default").toString());
        std::lock_guard lock(s_connectionsMutex);
        for (auto it = s_connections.begin(); it != s_connections.end();) {
            auto& connection = it->second;
            connection->workflows.erase(workflowKey);
            if (connection->workflows.empty()) {
                std::lock_guard ioLock(connection->mutex);
                if (connection->io) connection->io->close();
                it = s_connections.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        auto& d = context.data;
        const bool virt = d.value("virtualMode", false).toBool();
        const QString port = readPort(d, "COM3");
        const int baud = readBaud(d, 9600);
        const double volt = readVoltage(d, 3.3);
        const double curr = readCurrent(d, 1.0);
        const QString act = readAction(d, "on");
        const int delay = readDelay(d, 0);

        QStringList trace;
        auto tr = [&](const QString& s) { trace.append(s); d.insert("power.scpiTrace", trace.join(" | ")); };
        auto er = [&](const QString& m) { errorMessage = trace.join(" | ") + " | " + m; };

        // 虚拟模式
        if (virt || port.isEmpty() || port.compare("VIRTUAL", Qt::CaseInsensitive) == 0) {
            tr("VIRTUAL MODE");
            if (act == "off") { d.insert("power.state", "off"); return true; }
            d.insert("power.state", "on"); d.insert("power.voltage", volt);
            d.insert("power.current", curr * 0.1); d.insert("power.ident", "VIRTUAL-PS");
            if (delay > 0) QThread::msleep(delay); return true;
        }

        // Workflow-scoped connection pool: reuse the serial session across
        // power_on, measurement and power_off steps, then close it in postWorkflow.
        const QString key = connectionKey(d);
        PowerConnection* connection = nullptr;
        {
            std::lock_guard lock(s_connectionsMutex);
            auto& slot = s_connections[key];
            if (!slot) slot = std::make_unique<PowerConnection>();
            connection = slot.get();
            connection->workflows.insert(QString("%1/%2").arg(
                context.workflowId, d.value("_cellId", "default").toString()));
            if (!connection->io) {
                connection->io = std::make_unique<eon::infra::SerialScpiIO>();
                connection->io->setDefaultBaudRate(baud);
            }
        }
        std::unique_lock ioLock(connection->mutex);
        auto* io = connection->io.get();
        QVariantMap ioCfg;
        ioCfg["port"] = port; ioCfg["baudRate"] = baud;
        ioCfg["dataBits"] = d.value("dataBits", 8);
        ioCfg["parity"] = d.value("parity", "N");
        ioCfg["stopBits"] = d.value("stopBits", 1);

        tr(QString("OPEN %1 @ %2").arg(port).arg(baud));
        if (!io->isConnected() && !io->open(ioCfg)) {
            er(QString("Cannot open %1").arg(port));
            return false;
        }

        auto sc = [&](const QString& cmd, int t = 200) {
            tr(">> " + cmd);
            return io->writeCommand(cmd, t);
        };
        auto sq = [&](const QString& cmd, int t = 1000) {
            tr(">> " + cmd);
            QString r = io->query(cmd, t);
            tr("<< " + (r.isEmpty() ? "<empty>" : r));
            return r;
        };

        sc("SYST:REM");
        if (act == "off") { sc("OUTPut OFF"); d.insert("power.state", "off"); return true; }

        QString idn = sq("*IDN?", 2000);
        if (idn.isEmpty()) { er("Device not responding to *IDN?"); return false; }
        d.insert("power.ident", idn);

        if (!sc(QString("VOLTage %1").arg(volt, 0, 'f', 3))) { er("Failed to set voltage"); return false; }
        if (!sc(QString("CURRent %1").arg(curr, 0, 'f', 3))) { er("Failed to set current"); return false; }

        if (!sc("OUTPut ON")) { er("Failed to turn output ON"); return false; }
        d.insert("power.state", "on");
        if (delay > 0) QThread::msleep(delay);

        QString vR = sq("MEASure:VOLTage?");
        bool vOk; double vV = vR.toDouble(&vOk);
        if (vOk) d.insert("power.voltage", vV);

        QString cR = sq("MEASure:CURRent?");
        bool cOk; double cV = cR.toDouble(&cOk);
        if (cOk) d.insert("power.current", cV);

        return true;
    }
};

#include "PowerSupplyPlugin.moc"
