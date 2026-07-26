#pragma once

#include "eon/sdk/IScpiIO.h"
#include "eon/sdk/ITransport.h"
#include <QSerialPort>

namespace eon::infra {

/// <summary>
/// 串口 SCPI I/O 实现（同时支持 IScpiIO 和 ITransport 接口）。
/// 使用 QSerialPort，配合 processEvents 确保完整读取（借鉴 RYCOM 模式）。
/// 非 SCPI 协议可通过 ITransport::readBytes/writeBytes 直接访问。
/// </summary>
class SerialScpiIO : public eon::sdk::IScpiIO, public eon::sdk::ITransport {
public:
    SerialScpiIO();
    ~SerialScpiIO() override;

    bool open(const QVariantMap& config) override;
    void close() override;
    bool isConnected() const override;
    bool deviceClear() override;
    bool writeCommand(const QString& cmd, int timeoutMs = 200) override;
    QString query(const QString& query, int timeoutMs = 1000) override;
    QString readError() override;
    QString configInfo() const override;

    // --- ITransport 接口 ---
    QByteArray readBytes(int timeoutMs) override;
    bool writeBytes(const QByteArray& data, int timeoutMs) override;
    QString transportName() const override { return QStringLiteral("Serial"); }
    eon::sdk::ITransport* transport() override { return this; }
    const eon::sdk::ITransport* transport() const override { return this; }

    /// 设置串口参数（用于 config 中没有的默认值）
    void setDefaultBaudRate(int baud) { defaultBaud_ = baud; }

private:
    QSerialPort* port_ = nullptr;
    int defaultBaud_ = 9600;
    QString portName_;
    int baudRate_ = 9600;
};

} // namespace eon::infra
