#include <eon/infra/PythonProcessBackend.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>

namespace eon::infra {

struct PythonProcessBackend::Impl {
    QProcess process;
    int defaultTimeoutMs = 10000;
    int requestId = 0;

    QString pythonPath;
    QString scriptPath;

    bool startProcess(const QVariantMap& config, QString& error) {
        pythonPath = config.value("command", "python3").toString();
        scriptPath = config.value("script").toString();
        const QString workDir = config.value("workDir").toString();
        defaultTimeoutMs = config.value("timeoutMs", 10000).toInt();

        if (scriptPath.isEmpty()) {
            error = "PythonProcessBackend: 'script' is required.";
            return false;
        }

        if (!workDir.isEmpty()) process.setWorkingDirectory(workDir);
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(pythonPath, {scriptPath});
        if (!process.waitForStarted(5000)) {
            error = QString("PythonProcessBackend: Failed to start %2 %3: %1")
                .arg(process.errorString(), pythonPath, scriptPath);
            return false;
        }
        return true;
    }
};

PythonProcessBackend::PythonProcessBackend() : impl_(std::make_unique<Impl>()) {}
PythonProcessBackend::~PythonProcessBackend() { close(); }

bool PythonProcessBackend::open(const QVariantMap& config, QString& errorMessage) {
    if (isOpen()) return true;
    return impl_->startProcess(config, errorMessage);
}

void PythonProcessBackend::close() {
    if (impl_->process.state() != QProcess::NotRunning) {
        impl_->process.write("{\"cmd\":\"shutdown\"}\n");
        impl_->process.waitForBytesWritten(500);
        if (!impl_->process.waitForFinished(2000)) {
            impl_->process.kill();
        }
    }
}

bool PythonProcessBackend::isOpen() const {
    return impl_->process.state() == QProcess::Running;
}

bool PythonProcessBackend::write(const QByteArray& data, int timeoutMs, QString& errorMessage) {
    qint64 written = impl_->process.write(data);
    if (written < 0) { errorMessage = "PythonProcessBackend: write failed."; return false; }
    if (!impl_->process.waitForBytesWritten(timeoutMs > 0 ? timeoutMs : 500)) {
        errorMessage = "PythonProcessBackend: write timeout.";
        return false;
    }
    return true;
}

QByteArray PythonProcessBackend::readUntil(const QByteArray& terminator, int timeoutMs, QString& errorMessage) {
    QByteArray buf;
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeoutMs) {
        impl_->process.waitForReadyRead(50);
        buf.append(impl_->process.readAllStandardOutput());
        if (buf.contains(terminator)) {
            int idx = buf.indexOf(terminator);
            if (idx >= 0) buf = buf.left(idx);
            return buf;
        }
    }
    errorMessage = "PythonProcessBackend: read timeout.";
    return buf;
}

QByteArray PythonProcessBackend::readLine(int timeoutMs, QString& errorMessage) {
    return readUntil("\n", timeoutMs, errorMessage);
}

void PythonProcessBackend::flush() {
    impl_->process.readAllStandardOutput(); // discard
}

QJsonObject PythonProcessBackend::sendRequest(const QJsonObject& request, int timeoutMs, QString& errorMessage) {
    QJsonObject req = request;
    int id = ++impl_->requestId;
    req["id"] = id;

    QByteArray data = QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n";
    if (!write(data, 500, errorMessage)) return {};

    QByteArray resp = readUntil("\n", timeoutMs, errorMessage);
    if (resp.isEmpty()) return {};

    QJsonParseError err;
    QJsonObject result = QJsonDocument::fromJson(resp, &err).object();
    if (err.error != QJsonParseError::NoError) {
        errorMessage = QString("PythonProcessBackend: JSON parse error: %1").arg(err.errorString());
        return {};
    }
    return result;
}

} // namespace eon::infra
