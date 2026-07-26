#include "eon/infra/SerialScpiIO.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMap>
#include <QThread>
#include <QVariant>

namespace eon::infra {

SerialScpiIO::SerialScpiIO() = default;

SerialScpiIO::~SerialScpiIO() {
    close();
}

bool SerialScpiIO::open(const QVariantMap& config) {
    close();

    portName_ = config.value("port", config.value("address", "COM1")).toString();
    baudRate_ = config.value("baudRate", defaultBaud_).toInt();
    int dataBits = config.value("dataBits", 8).toInt();
    QString parity = config.value("parity", "N").toString().toUpper();
    double stopBits = config.value("stopBits", 1).toDouble();
    QString flowCtrl = config.value("flowCtrl", "none").toString().toLower();

    port_ = new QSerialPort();
    port_->setPortName(portName_);
    port_->setBaudRate(baudRate_);
    port_->setDataBits(static_cast<QSerialPort::DataBits>(dataBits));
    port_->setParity(parity == "E" ? QSerialPort::EvenParity :
                     parity == "O" ? QSerialPort::OddParity : QSerialPort::NoParity);
    port_->setStopBits(stopBits == 1.5 ? QSerialPort::OneAndHalfStop :
                       stopBits == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop);
    port_->setFlowControl(flowCtrl == "hardware" ? QSerialPort::HardwareControl :
                          flowCtrl == "software" ? QSerialPort::SoftwareControl :
                          QSerialPort::NoFlowControl);

    if (!port_->open(QIODevice::ReadWrite)) {
        delete port_;
        port_ = nullptr;
        return false;
    }
    return true;
}

void SerialScpiIO::close() {
    if (port_) {
        if (port_->isOpen()) port_->close();
        delete port_;
        port_ = nullptr;
    }
}

bool SerialScpiIO::isConnected() const {
    return port_ && port_->isOpen();
}

bool SerialScpiIO::deviceClear() {
    return writeCommand("*CLS");
}

bool SerialScpiIO::writeCommand(const QString& cmd, int timeoutMs) {
    if (!port_ || !port_->isOpen()) return false;
    port_->write((cmd + "\n").toUtf8());
    return port_->waitForBytesWritten(timeoutMs);
}

QString SerialScpiIO::query(const QString& query, int timeoutMs) {
    if (!port_ || !port_->isOpen()) return {};
    port_->write((query + "\n").toUtf8());
    if (!port_->waitForBytesWritten(200)) return {};

    // RYCOM 模式：用 processEvents 让 readyRead 信号触发，填满缓冲区
    QElapsedTimer et;
    et.start();
    QByteArray buf;
    while (et.elapsed() < timeoutMs && !buf.contains('\n')) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        buf.append(port_->readAll());
    }
    int nl = buf.indexOf('\n');
    if (nl >= 0) buf.truncate(nl);
    return QString::fromUtf8(buf).trimmed();
}

QString SerialScpiIO::readError() {
    return query("SYST:ERR?").trimmed();
}

QString SerialScpiIO::configInfo() const {
    if (!port_)
        return QString("SerialScpiIO [closed]");
    return QString("SerialScpiIO [%1 @ %2, 8%3%4]")
        .arg(portName_)
        .arg(baudRate_)
        .arg(port_->parity() == QSerialPort::EvenParity ? "E" :
             port_->parity() == QSerialPort::OddParity ? "O" : "N")
        .arg(port_->stopBits() == QSerialPort::OneStop ? "1" : "2");
}

// ============================================================
// ITransport 接口
// ============================================================

QByteArray SerialScpiIO::readBytes(int timeoutMs)
{
    if (!port_ || !port_->isOpen()) return {};
    QByteArray buf;
    QElapsedTimer et;
    et.start();
    while (et.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        if (port_->bytesAvailable() > 0)
            buf.append(port_->readAll());
        if (!buf.isEmpty()) break;
    }
    return buf;
}

bool SerialScpiIO::writeBytes(const QByteArray& data, int timeoutMs)
{
    if (!port_ || !port_->isOpen()) return false;
    port_->write(data);
    return port_->waitForBytesWritten(timeoutMs);
}

} // namespace eon::infra
