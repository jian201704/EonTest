#pragma once

#include <eon/sdk/InstrumentDriver.h>
#include <memory>

namespace eon::infra {

// ============================================================================
// PowerSupplyDriver — 可编程电源驱动（SCPI）
//
// 支持命令：
//   execute("on",  {{"voltage", 3.3}, {"current", 1.0}, {"ovp", 3.6}, ...})
//   execute("off", {})
//   query("measure", {}) → {"voltage": 3.301, "current": 0.85}
//   query("identify", {}) → {"ident": "ITECH,IT6723C,..."}
//
// execute("on") 发送序列：SYST:REM → VOLTage X → CURRent X → OUTPut ON → 回读
// execute("off") 发送序列：SYST:REM → OUTPut OFF
// ============================================================================
class PowerSupplyDriver final : public eon::sdk::InstrumentDriver {
    Q_OBJECT
public:
    explicit PowerSupplyDriver(QObject* parent = nullptr);
    ~PowerSupplyDriver() override;

    QString driverType() const override { return "power.supply"; }

    bool open(eon::sdk::IBackend* backend, const QVariantMap& config, QString& errorMessage) override;
    void close() override;
    bool isOpen() const override;

    bool execute(const QString& command, const QVariantMap& params, QString& errorMessage) override;
    QVariantMap query(const QString& command, const QVariantMap& params, QString& errorMessage) override;

    QString identity() const override;
    QString scpiTrace() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eon::infra
