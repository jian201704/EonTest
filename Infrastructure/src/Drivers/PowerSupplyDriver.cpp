#include <eon/infra/PowerSupplyDriver.h>
#include <eon/sdk/IBackend.h>
#include <QStringList>
#include <QThread>

namespace eon::infra {

struct PowerSupplyDriver::Impl {
    eon::sdk::IBackend* backend = nullptr;
    QString ident;
    QStringList traceLog;

    void tr(const QString& s) { traceLog.append(s); }
    QString trace() const { return traceLog.join(" | "); }
    void clearTrace() { traceLog.clear(); }

    bool scpiWrite(const QString& cmd, int t, QString& err) {
        return backend->write((cmd + "\n").toUtf8(), t, err);
    }
    QString scpiRead(int t, QString& err) {
        QByteArray data = backend->readUntil("\n", t, err);
        return QString::fromUtf8(data).trimmed();
    }
    QString scpiQuery(const QString& cmd, int t, QString& err) {
        if (!scpiWrite(cmd, t, err)) return {};
        return scpiRead(t, err);
    }
};

PowerSupplyDriver::PowerSupplyDriver(QObject* parent)
    : InstrumentDriver(parent), impl_(std::make_unique<Impl>()) {}

PowerSupplyDriver::~PowerSupplyDriver() { close(); }

bool PowerSupplyDriver::open(eon::sdk::IBackend* backend, const QVariantMap& config, QString& errorMessage) {
    if (!backend) { errorMessage = "PowerSupplyDriver: backend is null."; return false; }
    impl_->backend = backend;
    impl_->clearTrace();

    const int baud = config.value("baudRate", 9600).toInt();
    const QString port = config.value("port", config.value("address", "unknown")).toString();
    impl_->tr(QString("OPEN %1 @ %2").arg(port).arg(baud));

    if (!backend->isOpen()) {
        QVariantMap beCfg;
        beCfg["port"] = port;
        beCfg["baudRate"] = baud;
        beCfg["dataBits"] = config.value("dataBits", 8);
        beCfg["parity"] = config.value("parity", "none");
        beCfg["stopBits"] = config.value("stopBits", 1);
        beCfg["flowCtrl"] = config.value("flowCtrl", "none");
        if (!backend->open(beCfg, errorMessage)) {
            impl_->tr(QString("OPEN FAIL: %1").arg(errorMessage));
            return false;
        }
    }

    // 远程模式 + 识别
    impl_->tr(">> SYST:REM");
    impl_->scpiWrite("SYST:REM", 300, errorMessage);

    impl_->tr(">> *IDN?");
    impl_->ident = impl_->scpiQuery("*IDN?", 500, errorMessage);
    impl_->tr(QString("<< %1").arg(impl_->ident.isEmpty() ? "<empty>" : impl_->ident));

    return true;
}

void PowerSupplyDriver::close() {
    if (impl_->backend) {
        impl_->backend->close();
        impl_->backend = nullptr;
    }
}

bool PowerSupplyDriver::isOpen() const {
    return impl_->backend && impl_->backend->isOpen();
}

bool PowerSupplyDriver::execute(const QString& command, const QVariantMap& params, QString& errorMessage) {
    if (!impl_->backend || !impl_->backend->isOpen()) {
        errorMessage = "PowerSupplyDriver: not connected.";
        return false;
    }

    if (command == "off") {
        impl_->clearTrace();
        impl_->tr(">> OUTPut OFF");
        return impl_->scpiWrite("OUTPut OFF", 300, errorMessage);
    }

    if (command == "on" || command.isEmpty()) {
        impl_->clearTrace();
        const double volt = params.value("voltage", 3.3).toDouble();
        const double curr = params.value("current", 1.0).toDouble();
        const double ovp  = params.value("ovp", 0.0).toDouble();
        const double ocp  = params.value("ocp", 0.0).toDouble();
        const int    ch   = params.value("channel", 1).toInt();
        const int    delay = params.value("delay", 0).toInt();

        impl_->tr(">> SYST:REM");
        impl_->scpiWrite("SYST:REM", 300, errorMessage);

        impl_->tr(QString(">> VOLTage %1").arg(volt, 0, 'f', 3));
        if (!impl_->scpiWrite(QString("VOLTage %1").arg(volt, 0, 'f', 3), 300, errorMessage))
            return false;

        impl_->tr(QString(">> CURRent %1").arg(curr, 0, 'f', 3));
        if (!impl_->scpiWrite(QString("CURRent %1").arg(curr, 0, 'f', 3), 300, errorMessage))
            return false;

        if (ovp > 0.0) {
            impl_->tr(QString(">> VOLTage:PROTection %1").arg(ovp, 0, 'f', 3));
            impl_->scpiWrite(QString("VOLTage:PROTection %1").arg(ovp, 0, 'f', 3), 300, errorMessage);
        }
        if (ocp > 0.0) {
            impl_->tr(QString(">> CURRent:PROTection %1").arg(ocp, 0, 'f', 3));
            impl_->scpiWrite(QString("CURRent:PROTection %1").arg(ocp, 0, 'f', 3), 300, errorMessage);
        }
        if (ch > 1) {
            impl_->tr(QString(">> INSTrument:NSELect %1").arg(ch));
            impl_->scpiWrite(QString("INSTrument:NSELect %1").arg(ch), 300, errorMessage);
        }

        impl_->tr(">> OUTPut ON");
        if (!impl_->scpiWrite("OUTPut ON", 300, errorMessage)) return false;

        if (delay > 0) QThread::msleep(delay);

        // 回读
        impl_->tr(">> MEASure:VOLTage?");
        QString vResp = impl_->scpiQuery("MEASure:VOLTage?", 500, errorMessage);
        impl_->tr(QString("<< %1").arg(vResp.isEmpty() ? "<empty>" : vResp));

        impl_->tr(">> MEASure:CURRent?");
        QString cResp = impl_->scpiQuery("MEASure:CURRent?", 500, errorMessage);
        impl_->tr(QString("<< %1").arg(cResp.isEmpty() ? "<empty>" : cResp));
        return true;
    }

    errorMessage = QString("PowerSupplyDriver: unknown command '%1'").arg(command);
    return false;
}

QVariantMap PowerSupplyDriver::query(const QString& command, const QVariantMap& params, QString& errorMessage) {
    if (!impl_->backend || !impl_->backend->isOpen()) {
        errorMessage = "PowerSupplyDriver: not connected.";
        return {};
    }

    if (command == "measure") {
        impl_->tr(">> MEASure:VOLTage?");
        QString vResp = impl_->scpiQuery("MEASure:VOLTage?", 500, errorMessage);
        impl_->tr(QString("<< %1").arg(vResp.isEmpty() ? "<empty>" : vResp));

        impl_->tr(">> MEASure:CURRent?");
        QString cResp = impl_->scpiQuery("MEASure:CURRent?", 500, errorMessage);
        impl_->tr(QString("<< %1").arg(cResp.isEmpty() ? "<empty>" : cResp));

        QVariantMap result;
        bool ok = false;
        double v = vResp.toDouble(&ok);
        if (ok) result["voltage"] = v;
        double c = cResp.toDouble(&ok);
        if (ok) result["current"] = c;
        return result;
    }

    if (command == "identify") {
        QVariantMap result;
        result["ident"] = impl_->ident;
        return result;
    }

    errorMessage = QString("PowerSupplyDriver: unknown query '%1'").arg(command);
    return {};
}

QString PowerSupplyDriver::identity() const { return impl_->ident; }
QString PowerSupplyDriver::scpiTrace() const { return impl_->trace(); }

} // namespace eon::infra
