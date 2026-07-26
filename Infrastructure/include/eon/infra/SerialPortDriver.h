#pragma once

#include <QObject>

#include "eon/sdk/IDriverPlugin.h"

// 前向声明避免强依赖 QSerialPort 头
class QSerialPort;

namespace eon::infra {

// ============================================================================
// SerialPortDriver — 串口总线驱动
// 基于 Qt QSerialPort，支持 RS232/RS485/TTL
// ============================================================================
class SerialPortDriver final : public QObject, public eon::sdk::IBusDriver {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IBUSDRIVER_IID FILE "serialport.json")
    Q_INTERFACES(eon::sdk::IBusDriver)

public:
    explicit SerialPortDriver(QObject* parent = nullptr);
    ~SerialPortDriver() override;

    // --- IDriverPlugin ---
    QString id() const override { return "eon.driver.serialport"; }
    QString displayName() const override { return "Serial Port Driver"; }
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
    eon::sdk::BusType busType() const override { return eon::sdk::BusType::Serial; }

    bool send(const eon::sdk::BusFrame& frame, QString& errorMessage) override;
    bool receive(eon::sdk::BusFrame& frame, int timeoutMs, QString& errorMessage) override;
    void flushReceiveBuffer() override;
    void flushSendBuffer() override;
    QVariantMap errorCounters() const override;

    /// 枚举可用串口
    static QStringList availablePorts();

private:
    static bool parseParity(const QString& text, int* parityOut);
    static bool parseFlowControl(const QString& text, int* flowOut);

    bool initialized_ = false;
    QSerialPort* port_ = nullptr;
};

} // namespace eon::infra
