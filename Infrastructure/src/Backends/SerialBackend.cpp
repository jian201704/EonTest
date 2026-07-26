#include <eon/infra/SerialBackend.h>

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QDateTime>
#include <QThread>

namespace eon::infra {

struct SerialBackend::Impl {
    QSerialPort port;
    int writeTimeoutMs = 500;

    static QSerialPort::DataBits toDataBits(int n) {
        switch (n) { case 5: return QSerialPort::Data5; case 6: return QSerialPort::Data6; case 7: return QSerialPort::Data7; default: return QSerialPort::Data8; }
    }
    static QSerialPort::Parity toParity(const QString& s) {
        const auto lower = s.toLower().trimmed();
        if (lower == "odd")  return QSerialPort::OddParity;
        if (lower == "even") return QSerialPort::EvenParity;
        return QSerialPort::NoParity;
    }
    static QSerialPort::StopBits toStopBits(int n) {
        return n == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop;
    }
    static QSerialPort::FlowControl toFlow(const QString& s) {
        const auto lower = s.toLower().trimmed();
        if (lower == "rtscts")  return QSerialPort::HardwareControl;
        if (lower == "xonxoff") return QSerialPort::SoftwareControl;
        return QSerialPort::NoFlowControl;
    }
};

SerialBackend::SerialBackend() : impl_(std::make_unique<Impl>()) {}
SerialBackend::~SerialBackend() { close(); }

bool SerialBackend::open(const QVariantMap& config, QString& errorMessage) {
    if (impl_->port.isOpen()) return true;

    const QString portName = config.value("port").toString();
    if (portName.isEmpty()) {
        errorMessage = "SerialBackend: 'port' is required.";
        return false;
    }

    impl_->port.setPortName(portName);
    impl_->port.setBaudRate(config.value("baudRate", 9600).toInt());
    impl_->port.setDataBits(Impl::toDataBits(config.value("dataBits", 8).toInt()));
    impl_->port.setParity(Impl::toParity(config.value("parity", "none").toString()));
    impl_->port.setStopBits(Impl::toStopBits(config.value("stopBits", 1).toInt()));
    impl_->port.setFlowControl(Impl::toFlow(config.value("flowCtrl", "none").toString()));
    impl_->writeTimeoutMs = config.value("writeTimeoutMs", 500).toInt();

    if (!impl_->port.open(QIODevice::ReadWrite)) {
        errorMessage = QString("SerialBackend: Cannot open %1: %2")
            .arg(portName, impl_->port.errorString());
        return false;
    }
    impl_->port.clear(QSerialPort::AllDirections);
    return true;
}

void SerialBackend::close() {
    if (impl_->port.isOpen())
        impl_->port.close();
}

bool SerialBackend::isOpen() const {
    return impl_->port.isOpen();
}

bool SerialBackend::write(const QByteArray& data, int timeoutMs, QString& errorMessage) {
    if (!impl_->port.isOpen()) {
        errorMessage = "SerialBackend: Port not open.";
        return false;
    }
    const qint64 written = impl_->port.write(data);
    if (written < 0) {
        errorMessage = QString("SerialBackend: Write error: %1").arg(impl_->port.errorString());
        return false;
    }
    if (written < data.size()) {
        errorMessage = QString("SerialBackend: Partial write %1/%2 bytes").arg(written).arg(data.size());
        return false;
    }
    if (!impl_->port.waitForBytesWritten(timeoutMs > 0 ? timeoutMs : impl_->writeTimeoutMs)) {
        errorMessage = "SerialBackend: Write timeout.";
        return false;
    }
    return true;
}

QByteArray SerialBackend::readUntil(const QByteArray& terminator, int timeoutMs, QString& errorMessage) {
    if (!impl_->port.isOpen()) {
        errorMessage = "SerialBackend: Port not open.";
        return {};
    }

    QByteArray buf;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;

    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!impl_->port.waitForReadyRead(50)) {
            if (!buf.isEmpty()) break; // 已收到部分数据
            continue;                   // 还没数据，继续等
        }
        buf.append(impl_->port.readAll());
        if (buf.contains(terminator)) break;
        if (buf.size() > 65536) break; // 安全上限 64KB
    }

    if (buf.isEmpty()) {
        errorMessage = "SerialBackend: Read timeout.";
        return {};
    }

    // 截断到终止符
    const int idx = buf.indexOf(terminator);
    if (idx >= 0) buf = buf.left(idx);
    return buf;
}

QByteArray SerialBackend::readLine(int timeoutMs, QString& errorMessage) {
    return readUntil("\n", timeoutMs, errorMessage);
}

void SerialBackend::flush() {
    if (impl_->port.isOpen())
        impl_->port.clear(QSerialPort::Input);
}

} // namespace eon::infra
