#pragma once

#include <QObject>

#include "eon/sdk/IDriverPlugin.h"

// 前向声明
class QTcpSocket;
class QUdpSocket;

namespace eon::infra {

// ============================================================================
// TcpBusDriver — TCP/IP 总线驱动
// 用于 DoIP (ISO 13400)、TCP 透传等场景
//
// 特性:
//   - 支持 TCP 客户端模式
//   - 可选 UDP 发现通道（DoIP vehicle discovery）
//   - 连接保活与自动重连
// ============================================================================
class TcpBusDriver final : public QObject, public eon::sdk::IBusDriver {
public:
    explicit TcpBusDriver(QObject* parent = nullptr);
    ~TcpBusDriver() override;

    // --- IDriverPlugin ---
    QString id() const override { return "eon.driver.tcpbus"; }
    QString displayName() const override { return "TCP Bus Driver"; }
    QString version() const override { return "1.0.0"; }
    QVariantMap capabilities() const override;
    bool initialize(QString& errorMessage) override;
    void shutdown() override;
    bool isInitialized() const override { return initialized_; }

    // --- IBusDriver ---
    bool open(const eon::sdk::BusConfig& config, QString& errorMessage) override;
    void close() override;
    bool isOpen() const override;
    eon::sdk::BusDriverState state() const override;
    eon::sdk::BusType busType() const override { return eon::sdk::BusType::TCP; }

    bool send(const eon::sdk::BusFrame& frame, QString& errorMessage) override;
    bool receive(eon::sdk::BusFrame& frame, int timeoutMs, QString& errorMessage) override;
    void flushReceiveBuffer() override;
    void flushSendBuffer() override;
    QVariantMap errorCounters() const override;

    // --- TcpBus 特有 ---

    /// 获取底层 QTcpSocket（供协议层直接使用）
    QTcpSocket* tcpSocket() const;

    /// 获取 UDP 发现 socket
    QUdpSocket* udpSocket() const;

    /// 发送 UDP 广播/多播帧（用于 DoIP vehicle discovery）
    bool sendUdp(const QByteArray& data, const QString& host, quint16 port,
                 QString& errorMessage);

    /// 接收 UDP 帧
    bool receiveUdp(QByteArray& data, QString& sourceHost, quint16& sourcePort,
                    int timeoutMs, QString& errorMessage);

    /// TCP 是否已连接
    bool isTcpConnected() const;

private:
    bool initialized_ = false;
    QString host_;
    quint16 port_ = 0;
    QTcpSocket* tcpSocket_ = nullptr;
    QUdpSocket* udpSocket_ = nullptr;
};

} // namespace eon::infra
