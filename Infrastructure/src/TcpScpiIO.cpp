#include "eon/infra/TcpScpiIO.h"
#include <QCoreApplication>
#include <QElapsedTimer>

namespace eon::infra {

TcpScpiIO::TcpScpiIO() = default;

TcpScpiIO::~TcpScpiIO() {
    close();
}

bool TcpScpiIO::open(const QVariantMap& config) {
    close();

    host_ = config.value("host", "192.168.1.1").toString();
    tcpPort_ = config.value("tcpPort", config.value("port", 5025)).toInt();

    socket_ = new QTcpSocket();
    socket_->connectToHost(host_, tcpPort_);
    if (!socket_->waitForConnected(3000)) {
        delete socket_;
        socket_ = nullptr;
        return false;
    }
    return true;
}

void TcpScpiIO::close() {
    if (socket_) {
        if (socket_->isOpen()) socket_->close();
        delete socket_;
        socket_ = nullptr;
    }
}

bool TcpScpiIO::isConnected() const {
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

bool TcpScpiIO::deviceClear() {
    return writeCommand("*CLS");
}

bool TcpScpiIO::writeCommand(const QString& cmd, int timeoutMs) {
    if (!socket_ || !socket_->isOpen()) return false;
    socket_->write((cmd + "\n").toUtf8());
    return socket_->waitForBytesWritten(timeoutMs);
}

QString TcpScpiIO::query(const QString& query, int timeoutMs) {
    if (!socket_ || !socket_->isOpen()) return {};
    socket_->write((query + "\n").toUtf8());
    if (!socket_->waitForBytesWritten(200)) return {};

    QElapsedTimer et;
    et.start();
    QByteArray buf;
    while (et.elapsed() < timeoutMs && !buf.contains('\n')) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        if (socket_->bytesAvailable() > 0)
            buf.append(socket_->readAll());
    }
    int nl = buf.indexOf('\n');
    if (nl >= 0) buf.truncate(nl);
    return QString::fromUtf8(buf).trimmed();
}

QString TcpScpiIO::readError() {
    return query("SYST:ERR?").trimmed();
}

QString TcpScpiIO::configInfo() const {
    if (!socket_)
        return QString("TcpScpiIO [closed]");
    return QString("TcpScpiIO [%1:%2]")
        .arg(host_)
        .arg(tcpPort_);
}

// ============================================================
// ITransport 接口
// ============================================================

QByteArray TcpScpiIO::readBytes(int timeoutMs)
{
    if (!socket_ || !socket_->isOpen()) return {};
    if (!socket_->waitForReadyRead(timeoutMs)) return {};
    return socket_->readAll();
}

bool TcpScpiIO::writeBytes(const QByteArray& data, int timeoutMs)
{
    if (!socket_ || !socket_->isOpen()) return false;
    socket_->write(data);
    return socket_->waitForBytesWritten(timeoutMs);
}

} // namespace eon::infra
