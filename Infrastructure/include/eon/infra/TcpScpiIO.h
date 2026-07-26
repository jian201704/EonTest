#pragma once

#include "eon/sdk/IScpiIO.h"
#include "eon/sdk/ITransport.h"
#include <QTcpSocket>

namespace eon::infra {

/// <summary>
/// TCP/IP SCPI I/O 实现（同时支持 IScpiIO 和 ITransport 接口）。
/// 标准 SCPI 端口 5025，通过 raw socket 发送 "\n" 终止的命令。
/// 非 SCPI 协议可通过 ITransport::readBytes/writeBytes 直接访问。
/// </summary>
class TcpScpiIO : public eon::sdk::IScpiIO, public eon::sdk::ITransport {
public:
    TcpScpiIO();
    ~TcpScpiIO() override;

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
    QString transportName() const override { return QStringLiteral("TCP"); }
    eon::sdk::ITransport* transport() override { return this; }
    const eon::sdk::ITransport* transport() const override { return this; }

private:
    QTcpSocket* socket_ = nullptr;
    QString host_;
    int tcpPort_ = 5025;
};

} // namespace eon::infra
