#pragma once

#include <QObject>
#include <memory>

#include <eon/sdk/IBackend.h>

class QSerialPort;

namespace eon::infra {

// ============================================================================
// SerialBackend — 串口后端（Qt QSerialPort）
//
// config 参数：
//   port       - 串口号，如 "COM5"
//   baudRate   - 波特率，默认 9600
//   dataBits   - 数据位 5|6|7|8，默认 8
//   parity     - 校验 "none"|"odd"|"even"，默认 "none"
//   stopBits   - 停止位 1|2，默认 1
//   flowCtrl   - 流控 "none"|"rtscts"|"xonxoff"，默认 "none"
// ============================================================================
class SerialBackend final : public eon::sdk::IBackend {
public:
    SerialBackend();
    ~SerialBackend() override;

    // --- IBackend ---
    bool open(const QVariantMap& config, QString& errorMessage) override;
    void close() override;
    bool isOpen() const override;
    bool write(const QByteArray& data, int timeoutMs, QString& errorMessage) override;
    QByteArray readUntil(const QByteArray& terminator, int timeoutMs, QString& errorMessage) override;
    QByteArray readLine(int timeoutMs, QString& errorMessage) override;
    void flush() override;
    QString typeName() const override { return "serial"; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eon::infra
