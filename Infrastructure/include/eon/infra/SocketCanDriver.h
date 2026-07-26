#pragma once

#include <QObject>

#include "eon/sdk/IDriverPlugin.h"

namespace eon::infra {

// ============================================================================
// SocketCanDriver — Linux SocketCAN 总线驱动
// 基于 Linux SocketCAN 子系统 (AF_CAN + CAN_RAW)
// 仅在 Linux 平台编译
//
// 依赖:
//   - Linux kernel >= 3.6 (CAN FD >= 3.15)
//   - can-utils (ip link set can0 type can ...)
// ============================================================================
class SocketCanDriver final : public QObject, public eon::sdk::IBusDriver {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IBUSDRIVER_IID FILE "socketcan.json")
    Q_INTERFACES(eon::sdk::IBusDriver)

public:
    explicit SocketCanDriver(QObject* parent = nullptr);
    ~SocketCanDriver() override;

    // --- IDriverPlugin ---
    QString id() const override { return "eon.driver.socketcan"; }
    QString displayName() const override { return "SocketCAN Driver"; }
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
    eon::sdk::BusType busType() const override;

    bool send(const eon::sdk::BusFrame& frame, QString& errorMessage) override;
    bool receive(eon::sdk::BusFrame& frame, int timeoutMs, QString& errorMessage) override;
    void flushReceiveBuffer() override;
    void flushSendBuffer() override;
    QVariantMap errorCounters() const override;

private:
    bool initialized_ = false;
    int socketFd_ = -1;
    QString ifName_;
};

} // namespace eon::infra
