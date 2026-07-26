#pragma once

#include <QList>
#include <QMutex>
#include <QObject>
#include <QWaitCondition>

#include "eon/sdk/IDriverPlugin.h"

namespace eon::infra {

// ============================================================================
// VirtualBusDriver — 虚拟总线驱动
// 用于无硬件环境下的调试、回放和单元测试
//
// 支持:
//   - 软件回环 (loopback)：发送的帧立即出现在接收队列
//   - 回放模式 (replay)：预置帧序列逐条回放
//   - 录制模式 (record)：记录所有收发的帧
// ============================================================================
class VirtualBusDriver final : public QObject, public eon::sdk::IBusDriver {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IBUSDRIVER_IID FILE "virtualbus.json")
    Q_INTERFACES(eon::sdk::IBusDriver)

public:
    explicit VirtualBusDriver(QObject* parent = nullptr);
    ~VirtualBusDriver() override;

    // --- IDriverPlugin ---
    QString id() const override { return "eon.driver.virtualbus"; }
    QString displayName() const override { return "Virtual Bus Driver"; }
    QString version() const override { return "1.0.0"; }
    QVariantMap capabilities() const override;
    bool initialize(QString& errorMessage) override;
    void shutdown() override;
    bool isInitialized() const override { return initialized_; }

    // --- IBusDriver ---
    bool open(const eon::sdk::BusConfig& config, QString& errorMessage) override;
    void close() override;
    bool isOpen() const override { return opened_; }
    eon::sdk::BusDriverState state() const override;
    eon::sdk::BusType busType() const override { return eon::sdk::BusType::Virtual; }

    bool send(const eon::sdk::BusFrame& frame, QString& errorMessage) override;
    bool receive(eon::sdk::BusFrame& frame, int timeoutMs, QString& errorMessage) override;
    void flushReceiveBuffer() override;
    void flushSendBuffer() override;
    QVariantMap errorCounters() const override;

    // --- VirtualBus 特有 ---

    /// 预置回放帧序列
    void setReplayFrames(const QList<eon::sdk::BusFrame>& frames);

    /// 获取录制帧序列
    QList<eon::sdk::BusFrame> recordedFrames() const;

    /// 清零录制缓冲
    void clearRecordedFrames();

    /// 获取接收队列中的帧数
    int pendingFrameCount() const;

    /// 注入一帧到接收队列（模拟外部设备发数据）
    void injectFrame(const eon::sdk::BusFrame& frame);

    /// 设置帧延迟（模拟真实硬件延迟，单位 us）
    void setFrameDelayUs(qint64 delayUs);

private:
    bool initialized_ = false;
    bool opened_ = false;
    bool loopback_ = true;
    bool recording_ = false;
    qint64 frameDelayUs_ = 0;

    mutable QMutex mutex_;
    QWaitCondition frameAvailable_;

    QList<eon::sdk::BusFrame> replayFrames_;
    int replayIndex_ = 0;
    QList<eon::sdk::BusFrame> recordedFrames_;
    QList<eon::sdk::BusFrame> receiveQueue_;
    QList<eon::sdk::BusFrame> sendQueue_;
};

} // namespace eon::infra
