// Modbus 传输层实现 — 包含 Serial RTU 和 TCP/IP
#include "Transports.h"
#include <QByteArray>
#include <QSerialPort>
#include <QTcpSocket>
#include <QThread>
#include <cstdint>

// ============================================================
// CRC16-Modbus 计算
// ============================================================
static uint16_t crc16Modbus(const QByteArray& data) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= static_cast<uint8_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

QByteArray modbusAppendCRC(const QByteArray& frame) {
    uint16_t crc = crc16Modbus(frame);
    QByteArray out = frame;
    out.append(static_cast<char>(crc & 0xFF));       // 低字节在前
    out.append(static_cast<char>((crc >> 8) & 0xFF)); // 高字节在后
    return out;
}

bool verifyModbusCRC(const QByteArray& frame) {
    if (frame.size() < 3) return false;
    uint16_t crc = crc16Modbus(frame.left(frame.size() - 2));
    uint8_t lo = static_cast<uint8_t>(frame[frame.size() - 2]);
    uint8_t hi = static_cast<uint8_t>(frame[frame.size() - 1]);
    return (crc & 0xFF) == lo && ((crc >> 8) & 0xFF) == hi;
}

// ============================================================
// Modbus Serial RTU Transport
// ============================================================
class SerialModbusTransport {
    QSerialPort port_;
public:
    bool open(const QString& portName, int baudRate, int dataBits, QString parity, int stopBits) {
        if (port_.isOpen()) port_.close();
        port_.setPortName(portName);
        port_.setBaudRate(baudRate);

        switch (dataBits) {
            case 5: port_.setDataBits(QSerialPort::Data5); break;
            case 6: port_.setDataBits(QSerialPort::Data6); break;
            case 7: port_.setDataBits(QSerialPort::Data7); break;
            default: port_.setDataBits(QSerialPort::Data8); break;
        }

        if (parity.toLower() == "even") port_.setParity(QSerialPort::EvenParity);
        else if (parity.toLower() == "odd") port_.setParity(QSerialPort::OddParity);
        else port_.setParity(QSerialPort::NoParity);

        switch (stopBits) {
            case 2: port_.setStopBits(QSerialPort::TwoStop); break;
            default: port_.setStopBits(QSerialPort::OneStop); break;
        }

        // RS485 half-duplex: RTS on when writing
        port_.setFlowControl(QSerialPort::HardwareControl);

        return port_.open(QIODevice::ReadWrite);
    }

    void close() { if (port_.isOpen()) port_.close(); }
    bool isOpen() const { return port_.isOpen(); }

    bool writeFrame(const QByteArray& frame, int timeoutMs) {
        if (!port_.isOpen()) return false;
        port_.write(frame);
        return port_.waitForBytesWritten(timeoutMs);
    }

    QByteArray readResponse(int expectedLen, int timeoutMs) {
        QByteArray resp;
        if (!port_.waitForReadyRead(timeoutMs)) return resp;
        resp = port_.readAll();
        // Modbus RTU 帧可能分多次到达，短等待收齐
        QByteArray more;
        while (port_.waitForReadyRead(20)) {
            more = port_.readAll();
            if (more.isEmpty()) break;
            resp.append(more);
        }
        return resp;
    }

    void flush() { if (port_.isOpen()) port_.flush(); }
};

// ============================================================
// Modbus TCP Transport (跨平台 QTcpSocket，与 SerialBackend 相同 I/O 模式)
// ============================================================
class TcpModbusTransport {
    QTcpSocket socket_;
    uint16_t transactionId_ = 0;
    QString savedHost_;
    int savedPort_ = 0;

    QByteArray buildMBAP(uint16_t len, uint8_t unitId) {
        QByteArray mbap;
        mbap.append(static_cast<char>((transactionId_ >> 8) & 0xFF));
        mbap.append(static_cast<char>(transactionId_ & 0xFF));
        transactionId_++;
        mbap.append(static_cast<char>(0x00));
        mbap.append(static_cast<char>(0x00));
        mbap.append(static_cast<char>(((len + 1) >> 8) & 0xFF));
        mbap.append(static_cast<char>((len + 1) & 0xFF));
        mbap.append(static_cast<char>(unitId));
        return mbap;
    }
    // 确保连接已断开（与 SerialBackend open 风格一致）
    void ensureDisconnected() {
        if (socket_.state() != QAbstractSocket::UnconnectedState) {
            socket_.abort();
        }
    }

public:
    bool open(const QString& host, int port) {
        savedHost_ = host; savedPort_ = port;
        ensureDisconnected();
        socket_.connectToHost(host, static_cast<quint16>(port));
        return socket_.waitForConnected(5000);
    }
    void close() { ensureDisconnected(); }
    bool isOpen() const { return socket_.state() == QAbstractSocket::ConnectedState; }
    bool ensureConnected() {
        if (isOpen()) return true;
        return !savedHost_.isEmpty() && open(savedHost_, savedPort_);
    }
    // === SerialBackend 一致的模式: write → waitForBytesWritten → waitForReadyRead ===
    bool writeFrame(const QByteArray& pdu, uint8_t unitId, int timeoutMs) {
        if (!ensureConnected()) return false;
        QByteArray frame = buildMBAP(static_cast<uint16_t>(pdu.size()), unitId) + pdu;
        if (socket_.write(frame) != frame.size()) return false;
        bool ok = socket_.waitForBytesWritten(timeoutMs);
        return ok;
    }
    QByteArray readResponse(int timeoutMs) {
        if (!ensureConnected()) return {};
        bool ready = socket_.waitForReadyRead(timeoutMs);
        if (!ready) return {};

        // 读取 MBAP 头 (7 bytes)
        QByteArray header = socket_.read(7);
        if (header.size() < 7) return {};
        uint16_t len = (static_cast<uint8_t>(header[4]) << 8) | static_cast<uint8_t>(header[5]);
        if (len == 0) return header;

        // 剩余数据已在内核缓冲中，直接读取无需再次 waitForReadyRead
        QByteArray body = socket_.read(len);
        if (body.size() < len) {
            // 如果数据还没到齐，等一小会儿
            if (socket_.waitForReadyRead(500))
                body += socket_.read(len - body.size());
        }
        return header + body;
    }
    void flush() { if (socket_.isOpen()) socket_.flush(); }
};

// ============================================================
// ModbusHandle 实现（声明见 Transports.h）
// ============================================================

bool ModbusHandle::openSerial(const QString& port, int baud, int dataBits, const QString& parity, int stopBits) {
    auto* s = new SerialModbusTransport();
    if (!s->open(port, baud, dataBits, parity, stopBits)) { delete s; return false; }
    serial = s; type = ModbusTransportType::SerialRTU; return true;
}

bool ModbusHandle::openTCP(const QString& host, int port) {
    auto* t = new TcpModbusTransport();
    if (!t->open(host, port)) { delete t; return false; }
    tcp = t; type = ModbusTransportType::TCP; return true;
}

void ModbusHandle::close() {
    if (serial) { serial->close(); delete serial; serial = nullptr; }
    if (tcp) { tcp->close(); delete tcp; tcp = nullptr; }
    type = ModbusTransportType::Unknown;
}

bool ModbusHandle::writeFrame(const QByteArray& pdu, int timeoutMs) {
    if (type == ModbusTransportType::SerialRTU) {
        QByteArray addr; addr.append(static_cast<char>(slaveId));
        return serial->writeFrame(modbusAppendCRC(addr + pdu), timeoutMs);
    } else if (type == ModbusTransportType::TCP) {
        return tcp->writeFrame(pdu, unitId, timeoutMs);
    }
    return false;
}

QByteArray ModbusHandle::readResponse(int timeoutMs) {
    if (type == ModbusTransportType::SerialRTU) {
        return serial->readResponse(256, timeoutMs);
    } else if (type == ModbusTransportType::TCP) {
        return tcp->readResponse(timeoutMs);
    }
    return {};
}

void ModbusHandle::flush() {
    if (serial) serial->flush();
    if (tcp) tcp->flush();
}
