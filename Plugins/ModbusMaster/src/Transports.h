#pragma once
#include <QByteArray>
#include <QString>
#include <cstdint>

// Modbus 工具函数
inline uint16_t toU16(uint8_t hi, uint8_t lo) {
    return (static_cast<uint16_t>(hi) << 8) | static_cast<uint16_t>(lo);
}
inline int16_t toS16(uint8_t hi, uint8_t lo) {
    return static_cast<int16_t>(toU16(hi, lo));
}
QByteArray modbusAppendCRC(const QByteArray& frame);
bool verifyModbusCRC(const QByteArray& frame);

// 前向声明
class SerialModbusTransport;
class TcpModbusTransport;

enum class ModbusTransportType { Unknown, SerialRTU, TCP };

struct ModbusHandle {
    ModbusTransportType type = ModbusTransportType::Unknown;
    SerialModbusTransport* serial = nullptr;
    TcpModbusTransport* tcp = nullptr;
    uint8_t slaveId = 1;
    uint8_t unitId = 1;

    bool openSerial(const QString& port, int baud, int dataBits, const QString& parity, int stopBits);
    bool openTCP(const QString& host, int port);
    void close();
    bool writeFrame(const QByteArray& pdu, int timeoutMs);
    QByteArray readResponse(int timeoutMs);
    void flush();
};
