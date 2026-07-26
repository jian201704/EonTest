#include "eon/infra/TcpBusDriver.h"

#include <QDateTime>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QNetworkInterface>

namespace eon::infra {

TcpBusDriver::TcpBusDriver(QObject* parent)
    : QObject(parent) {}

TcpBusDriver::~TcpBusDriver() {
    shutdown();
}

QVariantMap TcpBusDriver::capabilities() const {
    return {
        {"busType", "tcp"},
        {"supportsUdp", true},
        {"supportsKeepAlive", true},
        {"maxFrameSize", 65535},
        {"protocols", QVariantList{"DoIP (ISO 13400)", "Raw TCP"}}
    };
}

bool TcpBusDriver::initialize(QString& errorMessage) {
    Q_UNUSED(errorMessage)
    if (initialized_) return true;
    initialized_ = true;
    return true;
}

void TcpBusDriver::shutdown() {
    close();
    initialized_ = false;
}

bool TcpBusDriver::open(const eon::sdk::BusConfig& config, QString& errorMessage) {
    if (!initialized_) {
        errorMessage = "TcpBus driver not initialized.";
        return false;
    }

    host_ = config.properties.value("host", "127.0.0.1").toString();
    port_ = static_cast<quint16>(config.properties.value("port", 13400).toUInt());

    // 创建 TCP socket
    if (!tcpSocket_) {
        tcpSocket_ = new QTcpSocket(this);
    }

    if (tcpSocket_->state() == QAbstractSocket::ConnectedState) {
        return true; // 已连接
    }

    // 可选：启用 TCP keepalive
    if (config.properties.value("keepAlive", true).toBool()) {
        tcpSocket_->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    }

    // 连接
    tcpSocket_->connectToHost(host_, port_);
    if (!tcpSocket_->waitForConnected(5000)) {
        errorMessage = QString("TCP connect to %1:%2 failed: %3")
                           .arg(host_).arg(port_).arg(tcpSocket_->errorString());
        return false;
    }

    // 可选：创建 UDP socket（用于 DoIP vehicle discovery）
    if (config.properties.value("enableUdp", true).toBool()) {
        if (!udpSocket_) {
            udpSocket_ = new QUdpSocket(this);
        }
        // 绑定到任意端口（用于接收 UDP 响应）
        if (udpSocket_->state() != QAbstractSocket::BoundState) {
            if (!udpSocket_->bind(QHostAddress::AnyIPv4, 0)) {
                qWarning() << "TcpBus: UDP bind failed:" << udpSocket_->errorString();
                // UDP 非关键，继续
            }
        }
    }

    return true;
}

void TcpBusDriver::close() {
    if (tcpSocket_) {
        tcpSocket_->close();
    }
    if (udpSocket_) {
        udpSocket_->close();
    }
}

bool TcpBusDriver::isOpen() const {
    return tcpSocket_ && tcpSocket_->state() == QAbstractSocket::ConnectedState;
}

bool TcpBusDriver::isTcpConnected() const {
    return isOpen();
}

eon::sdk::BusDriverState TcpBusDriver::state() const {
    if (!initialized_) return eon::sdk::BusDriverState::Disconnected;
    if (!tcpSocket_) return eon::sdk::BusDriverState::Disconnected;
    switch (tcpSocket_->state()) {
    case QAbstractSocket::ConnectedState:
        return eon::sdk::BusDriverState::Connected;
    case QAbstractSocket::ConnectingState:
    case QAbstractSocket::HostLookupState:
        return eon::sdk::BusDriverState::Connecting;
    default:
        return eon::sdk::BusDriverState::Disconnected;
    }
}

bool TcpBusDriver::send(const eon::sdk::BusFrame& frame, QString& errorMessage) {
    if (!tcpSocket_ || tcpSocket_->state() != QAbstractSocket::ConnectedState) {
        errorMessage = "TCP not connected.";
        return false;
    }

    const qint64 written = tcpSocket_->write(frame.data);
    if (written < 0) {
        errorMessage = QString("TCP send failed: %1").arg(tcpSocket_->errorString());
        return false;
    }
    if (written < frame.data.size()) {
        // 继续写剩余数据
        QByteArray remaining = frame.data.mid(static_cast<int>(written));
        while (!remaining.isEmpty()) {
            if (!tcpSocket_->waitForBytesWritten(5000)) {
                errorMessage = "TCP write timeout.";
                return false;
            }
            const qint64 more = tcpSocket_->write(remaining);
            if (more < 0) break;
            remaining = remaining.mid(static_cast<int>(more));
        }
    }
    return true;
}

bool TcpBusDriver::receive(eon::sdk::BusFrame& frame, int timeoutMs, QString& errorMessage) {
    if (!tcpSocket_ || tcpSocket_->state() != QAbstractSocket::ConnectedState) {
        errorMessage = "TCP not connected.";
        return false;
    }

    // 确保有数据可用
    if (tcpSocket_->bytesAvailable() == 0) {
        const int waitMs = (timeoutMs < 0) ? 30000 : timeoutMs;
        if (waitMs > 0) {
            if (!tcpSocket_->waitForReadyRead(waitMs)) {
                if (timeoutMs != 0) {
                    errorMessage = "TCP receive timeout.";
                } else {
                    errorMessage = "No data available.";
                }
                return false;
            }
        }
    }

    frame.data = tcpSocket_->readAll();
    if (frame.data.isEmpty()) {
        errorMessage = "No data received.";
        return false;
    }

    frame.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    frame.id = 0;
    return true;
}

void TcpBusDriver::flushReceiveBuffer() {
    if (tcpSocket_) {
        tcpSocket_->readAll();
    }
}

void TcpBusDriver::flushSendBuffer() {
    if (tcpSocket_) {
        tcpSocket_->flush();
    }
}

QVariantMap TcpBusDriver::errorCounters() const {
    if (!tcpSocket_) {
        return {{"error", "not initialized"}};
    }
    return {
        {"host", host_},
        {"port", port_},
        {"state", tcpSocket_->state()},
        {"bytesAvailable", tcpSocket_->bytesAvailable()},
        {"errorString", tcpSocket_->errorString()}
    };
}

// ============================================================================
// 特有方法
// ============================================================================

QTcpSocket* TcpBusDriver::tcpSocket() const {
    return tcpSocket_;
}

QUdpSocket* TcpBusDriver::udpSocket() const {
    return udpSocket_;
}

bool TcpBusDriver::sendUdp(const QByteArray& data, const QString& host, quint16 port,
                            QString& errorMessage) {
    if (!udpSocket_) {
        errorMessage = "UDP socket not initialized. Set enableUdp=true in config.";
        return false;
    }

    const qint64 written = udpSocket_->writeDatagram(data, QHostAddress(host), port);
    if (written < 0) {
        errorMessage = QString("UDP send failed: %1").arg(udpSocket_->errorString());
        return false;
    }
    if (written < data.size()) {
        errorMessage = "UDP partial send.";
        return false;
    }
    return true;
}

bool TcpBusDriver::receiveUdp(QByteArray& data, QString& sourceHost, quint16& sourcePort,
                               int timeoutMs, QString& errorMessage) {
    if (!udpSocket_) {
        errorMessage = "UDP socket not initialized.";
        return false;
    }

    if (!udpSocket_->waitForReadyRead(timeoutMs < 0 ? 30000 : timeoutMs)) {
        errorMessage = "UDP receive timeout.";
        return false;
    }

    QHostAddress sender;
    data.resize(static_cast<int>(udpSocket_->pendingDatagramSize()));
    const qint64 read = udpSocket_->readDatagram(data.data(), data.size(), &sender, &sourcePort);
    if (read < 0) {
        errorMessage = QString("UDP read failed: %1").arg(udpSocket_->errorString());
        return false;
    }

    data.resize(static_cast<int>(read));
    sourceHost = sender.toString();
    return true;
}

} // namespace eon::infra

#include "TcpBusDriver.moc"
