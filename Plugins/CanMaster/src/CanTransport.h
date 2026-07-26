#pragma once
#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <cstdint>

// ============================================================
// CAN 帧结构（同 SDK IDriverPlugin.h 中的 BusFrame）
// ============================================================
struct CanFrame {
    uint32_t id = 0;              // 11位标准帧 or 29位扩展帧
    bool extendedFormat = false;  // true = 29位扩展帧
    bool remoteFrame = false;     // true = 远程帧 RTR
    bool fdFrame = false;         // true = CAN FD
    QByteArray data;              // 0-8 (CAN) / 0-64 (CAN FD)
    int64_t timestampUs = 0;
};

// ============================================================
// CAN 传输层抽象
// ============================================================
class CanTransport {
public:
    virtual ~CanTransport() = default;
    virtual bool open(const QVariantMap& config, QString& error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool sendFrame(const CanFrame& frame, int timeoutMs) = 0;
    virtual CanFrame receiveFrame(int timeoutMs) = 0;
    virtual void flush() = 0;
    virtual QString transportName() const = 0;
};

// ============================================================
// PCAN Basic API 传输实现（Windows）
// ============================================================
#ifdef Q_OS_WIN
class PcanTransport : public CanTransport {
    void* handle_ = nullptr; // TPcanHandle
public:
    bool open(const QVariantMap& config, QString& error) override;
    void close() override;
    bool isOpen() const override;
    bool sendFrame(const CanFrame& frame, int timeoutMs) override;
    CanFrame receiveFrame(int timeoutMs) override;
    void flush() override;
    QString transportName() const override { return "pcan"; }
};
#endif

// ============================================================
// SocketCAN 传输实现（Linux）
// ============================================================
#ifdef Q_OS_LINUX
class SocketCanTransport : public CanTransport {
    int socketFd_ = -1;
public:
    bool open(const QVariantMap& config, QString& error) override;
    void close() override;
    bool isOpen() const override;
    bool sendFrame(const CanFrame& frame, int timeoutMs) override;
    CanFrame receiveFrame(int timeoutMs) override;
    void flush() override;
    QString transportName() const override { return "socketcan"; }
};
#endif
