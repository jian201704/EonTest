#include "eon/infra/SerialPortDriver.h"

#include <QDateTime>

#if !defined(EON_HAS_SERIALPORT)

// ============================================================================
// Stub 实现 — Qt6::SerialPort 不可用时
// ============================================================================
namespace eon::infra {

SerialPortDriver::SerialPortDriver(QObject* parent) : QObject(parent) {}
SerialPortDriver::~SerialPortDriver() { shutdown(); }
QVariantMap SerialPortDriver::capabilities() const { return {{"error", "Qt6::SerialPort not available"}}; }
bool SerialPortDriver::initialize(QString& errorMessage) { errorMessage = "Qt6::SerialPort not available."; return false; }
void SerialPortDriver::shutdown() { initialized_ = false; }
bool SerialPortDriver::open(const eon::sdk::BusConfig&, QString& errorMessage) { errorMessage = "Qt6::SerialPort not available."; return false; }
void SerialPortDriver::close() {}
bool SerialPortDriver::isOpen() const { return false; }
eon::sdk::BusDriverState SerialPortDriver::state() const { return eon::sdk::BusDriverState::Error; }
bool SerialPortDriver::send(const eon::sdk::BusFrame&, QString& errorMessage) { errorMessage = "Qt6::SerialPort not available."; return false; }
bool SerialPortDriver::receive(eon::sdk::BusFrame&, int, QString& errorMessage) { errorMessage = "Qt6::SerialPort not available."; return false; }
void SerialPortDriver::flushReceiveBuffer() {}
void SerialPortDriver::flushSendBuffer() {}
QVariantMap SerialPortDriver::errorCounters() const { return {{"error", "not available"}}; }
QStringList SerialPortDriver::availablePorts() { return {}; }

} // namespace eon::infra

#else

#include <QSerialPort>
#include <QSerialPortInfo>

namespace eon::infra {

SerialPortDriver::SerialPortDriver(QObject* parent)
    : QObject(parent) {}

SerialPortDriver::~SerialPortDriver() {
    shutdown();
}

QVariantMap SerialPortDriver::capabilities() const {
    return {
        {"busType", "serial"},
        {"supportsRs232", true},
        {"supportsRs485", false},
        {"baudRates", QVariantList{9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600}},
        {"dataBits", QVariantList{5, 6, 7, 8}},
        {"stopBits", QVariantList{1, 2}},
        {"parities", QVariantList{"none", "odd", "even"}},
        {"flowControls", QVariantList{"none", "rtscts", "xonxoff"}}
    };
}

bool SerialPortDriver::initialize(QString& errorMessage) {
    Q_UNUSED(errorMessage)
    if (initialized_) return true;
    initialized_ = true;
    return true;
}

void SerialPortDriver::shutdown() {
    close();
    initialized_ = false;
}

bool SerialPortDriver::open(const eon::sdk::BusConfig& config, QString& errorMessage) {
    if (!initialized_) {
        errorMessage = "SerialPort driver not initialized.";
        return false;
    }

    if (port_ && port_->isOpen()) {
        return true;
    }

    auto port = std::make_unique<QSerialPort>();
    const QString portName = config.properties.value("portName", config.busId).toString();
    port->setPortName(portName);

    const int baudRate = config.properties.value("baudRate", 115200).toInt();
    if (!port->setBaudRate(baudRate)) {
        errorMessage = QString("Unsupported baud rate: %1").arg(baudRate);
        return false;
    }

    const int dataBits = config.properties.value("dataBits", 8).toInt();
    switch (dataBits) {
    case 5: port->setDataBits(QSerialPort::Data5); break;
    case 6: port->setDataBits(QSerialPort::Data6); break;
    case 7: port->setDataBits(QSerialPort::Data7); break;
    case 8: port->setDataBits(QSerialPort::Data8); break;
    default:
        errorMessage = QString("Invalid data bits: %1").arg(dataBits);
        return false;
    }

    const int stopBits = config.properties.value("stopBits", 1).toInt();
    port->setStopBits(stopBits == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop);

    int parity = 0;
    if (!parseParity(config.properties.value("parity", "none").toString(), &parity)) {
        errorMessage = QString("Invalid parity: %1").arg(config.properties.value("parity").toString());
        return false;
    }
    port->setParity(static_cast<QSerialPort::Parity>(parity));

    int flow = 0;
    if (!parseFlowControl(config.properties.value("flowControl", "none").toString(), &flow)) {
        errorMessage = QString("Invalid flow control: %1").arg(config.properties.value("flowControl").toString());
        return false;
    }
    port->setFlowControl(static_cast<QSerialPort::FlowControl>(flow));

    if (!port->open(QIODevice::ReadWrite)) {
        errorMessage = QString("Cannot open serial port '%1': %2").arg(portName, port->errorString());
        return false;
    }

    port->clear(QSerialPort::AllDirections);
    port_ = port.release();
    return true;
}

void SerialPortDriver::close() {
    if (port_) {
        if (port_->isOpen()) port_->close();
        delete port_;
        port_ = nullptr;
    }
}

bool SerialPortDriver::isOpen() const { return port_ && port_->isOpen(); }

eon::sdk::BusDriverState SerialPortDriver::state() const {
    if (!initialized_) return eon::sdk::BusDriverState::Disconnected;
    if (!port_ || !port_->isOpen()) return eon::sdk::BusDriverState::Disconnected;
    return eon::sdk::BusDriverState::Connected;
}

bool SerialPortDriver::send(const eon::sdk::BusFrame& frame, QString& errorMessage) {
    if (!port_ || !port_->isOpen()) { errorMessage = "Serial port is not open."; return false; }
    const qint64 written = port_->write(frame.data);
    if (written < 0) { errorMessage = QString("Serial write error: %1").arg(port_->errorString()); return false; }
    if (written < frame.data.size()) { errorMessage = QString("Serial partial write: %1/%2 bytes").arg(written).arg(frame.data.size()); return false; }
    return port_->waitForBytesWritten(1000);
}

bool SerialPortDriver::receive(eon::sdk::BusFrame& frame, int timeoutMs, QString& errorMessage) {
    if (!port_ || !port_->isOpen()) { errorMessage = "Serial port is not open."; return false; }
    if (timeoutMs == 0) {
        const QByteArray data = port_->readAll();
        if (data.isEmpty()) { errorMessage = "No data available."; return false; }
        frame.data = data;
        frame.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        frame.id = 0;
        return true;
    }
    if (!port_->waitForReadyRead(timeoutMs < 0 ? 30000 : timeoutMs)) { errorMessage = "Serial receive timeout."; return false; }
    frame.data = port_->readAll();
    frame.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    frame.id = 0;
    return true;
}

void SerialPortDriver::flushReceiveBuffer() { if (port_ && port_->isOpen()) port_->clear(QSerialPort::Input); }
void SerialPortDriver::flushSendBuffer() { if (port_ && port_->isOpen()) port_->clear(QSerialPort::Output); }

QVariantMap SerialPortDriver::errorCounters() const {
    if (!port_) return {{"error", "not open"}};
    return {{"portName", port_->portName()}, {"errorString", port_->errorString()}, {"bytesAvailable", port_->bytesAvailable()}};
}

QStringList SerialPortDriver::availablePorts() {
    QStringList ports;
    for (const auto& info : QSerialPortInfo::availablePorts()) ports.append(info.portName());
    return ports;
}

bool SerialPortDriver::parseParity(const QString& text, int* parityOut) {
    const QString lower = text.toLower().trimmed();
    if (lower == "none" || lower.isEmpty()) { *parityOut = QSerialPort::NoParity; return true; }
    if (lower == "odd")  { *parityOut = QSerialPort::OddParity;  return true; }
    if (lower == "even") { *parityOut = QSerialPort::EvenParity; return true; }
    return false;
}

bool SerialPortDriver::parseFlowControl(const QString& text, int* flowOut) {
    const QString lower = text.toLower().trimmed();
    if (lower == "none" || lower.isEmpty())  { *flowOut = QSerialPort::NoFlowControl; return true; }
    if (lower == "rtscts") { *flowOut = QSerialPort::HardwareControl; return true; }
    if (lower == "xonxoff") { *flowOut = QSerialPort::SoftwareControl; return true; }
    return false;
}

} // namespace eon::infra

#endif // EON_HAS_SERIALPORT

