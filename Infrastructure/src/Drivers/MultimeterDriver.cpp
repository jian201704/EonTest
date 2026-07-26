#include <eon/infra/MultimeterDriver.h>
#include <eon/sdk/IBackend.h>
#include <QStringList>

namespace eon::infra {

struct MultimeterDriver::Impl {
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
        return QString::fromUtf8(backend->readUntil("\n", t, err)).trimmed();
    }
    QString scpiQuery(const QString& cmd, int t, QString& err) {
        if (!scpiWrite(cmd, t, err)) return {};
        return scpiRead(t, err);
    }
};

MultimeterDriver::MultimeterDriver(QObject* parent)
    : InstrumentDriver(parent), impl_(std::make_unique<Impl>()) {}

MultimeterDriver::~MultimeterDriver() { close(); }

bool MultimeterDriver::open(eon::sdk::IBackend* backend, const QVariantMap& config, QString& errorMessage) {
    if (!backend) { errorMessage = "MultimeterDriver: backend is null."; return false; }
    impl_->backend = backend;
    impl_->clearTrace();

    const int baud = config.value("baudRate", 9600).toInt();
    const QString port = config.value("port", config.value("address", "unknown")).toString();
    impl_->tr(QString("OPEN %1 @ %2").arg(port).arg(baud));

    if (!backend->isOpen()) {
        QVariantMap beCfg;
        beCfg["port"] = port;
        beCfg["baudRate"] = baud;
        beCfg["dataBits"] = 8;
        beCfg["parity"] = "none";
        beCfg["stopBits"] = 1;
        if (!backend->open(beCfg, errorMessage)) {
            impl_->tr(QString("OPEN FAIL: %1").arg(errorMessage));
            return false;
        }
    }

    impl_->tr(">> *IDN?");
    impl_->ident = impl_->scpiQuery("*IDN?", 500, errorMessage);
    impl_->tr(QString("<< %1").arg(impl_->ident.isEmpty() ? "<empty>" : impl_->ident));
    return true;
}

void MultimeterDriver::close() {
    if (impl_->backend) { impl_->backend->close(); impl_->backend = nullptr; }
}

bool MultimeterDriver::isOpen() const { return impl_->backend && impl_->backend->isOpen(); }

bool MultimeterDriver::execute(const QString& command, const QVariantMap& params, QString& errorMessage) {
    Q_UNUSED(command); Q_UNUSED(params);
    errorMessage = "MultimeterDriver: execute not supported, use query('measure').";
    return false;
}

QVariantMap MultimeterDriver::query(const QString& command, const QVariantMap& params, QString& errorMessage) {
    if (!impl_->backend || !impl_->backend->isOpen()) {
        errorMessage = "MultimeterDriver: not connected."; return {};
    }

    if (command == "measure") {
        impl_->clearTrace();
        const QString type = params.value("type", "VOLTage:DC").toString();
        const QString range = params.value("range", "AUTO").toString();
        const double resolution = params.value("resolution", 0.001).toDouble();
        const int samples = qBound(1, params.value("samples", 1).toInt(), 100);

        impl_->tr(QString(">> CONFigure:%1 %2,%3").arg(type, range).arg(resolution, 0, 'f', 4));
        impl_->scpiWrite(QString("CONFigure:%1 %2,%3").arg(type, range).arg(resolution, 0, 'f', 4), 500, errorMessage);

        double sum = 0.0;
        int valid = 0;
        for (int i = 0; i < samples; ++i) {
            impl_->tr(QString(">> READ? (#%1)").arg(i + 1));
            QString resp = impl_->scpiQuery("READ?", 2000, errorMessage);
            impl_->tr(QString("<< %1").arg(resp.isEmpty() ? "<empty>" : resp));
            bool ok = false; double val = resp.toDouble(&ok);
            if (ok) { sum += val; valid++; }
        }

        if (valid == 0) { errorMessage = "MultimeterDriver: all samples failed."; return {}; }

        QString unit = type.contains("CURR", Qt::CaseInsensitive) ? "A"
                     : type.contains("RES", Qt::CaseInsensitive) ? "Ohm" : "V";

        return {{"value", sum / valid}, {"unit", unit}, {"samples", valid}};
    }

    if (command == "identify") return {{"ident", impl_->ident}};

    errorMessage = QString("MultimeterDriver: unknown query '%1'").arg(command);
    return {};
}

QString MultimeterDriver::identity() const { return impl_->ident; }
QString MultimeterDriver::scpiTrace() const { return impl_->trace(); }

} // namespace eon::infra
