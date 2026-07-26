#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QThread>
#include <memory>

#include "eon/sdk/IScpiIO.h"
#include "eon/infra/SerialScpiIO.h"
#include "eon/sdk/IStepPlugin.h"

namespace {

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
    return d.value("action", d.value("powerAction", fallback)).toString().trimmed().toLower();
}
inline int readDelay(const QVariantMap& d, int fallback) {
    return d.value("delay", d.value("powerOnDelayMs", fallback)).toInt();
}

} // namespace

class PowerSupplyPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "powersupply.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override { return "power.supply"; }

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

        // IScpiIO 抽象层（串口实现）
        auto io = std::make_unique<eon::infra::SerialScpiIO>();
        io->setDefaultBaudRate(baud);
        QVariantMap ioCfg;
        ioCfg["port"] = port; ioCfg["baudRate"] = baud;
        ioCfg["dataBits"] = d.value("dataBits", 8);
        ioCfg["parity"] = d.value("parity", "N");
        ioCfg["stopBits"] = d.value("stopBits", 1);

        tr(QString("OPEN %1 @ %2").arg(port).arg(baud));
        if (!io->open(ioCfg)) { er(QString("Cannot open %1").arg(port)); return false; }

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
