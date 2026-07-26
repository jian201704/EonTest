#pragma once

#include "IDut.h"
#include <QString>

namespace eon::sdk {

/// <summary>
/// DUT 基类。
/// 对标 OpenTAP Dut : Resource, IDut。
/// 提供 IDut 接口的默认实现，子类只需重写 dutId() 和 modelName()。
/// open()/close() 默认空实现（无硬件操作），子类可按需重写。
/// </summary>
class Dut : public IDut {
public:
    explicit Dut(const QString& id = {}, const QString& model = {});
    ~Dut() override = default;

    // ── IResource ──────────────────────────────────────────────
    bool open() override;
    void close() override;
    QString name() const override { return dutId_; }
    bool isConnected() const override { return connected_; }

    // ── IDut ───────────────────────────────────────────────────
    QString dutId() const override { return dutId_; }
    void setDutId(const QString& id) { dutId_ = id; }

    QString modelName() const override { return modelName_; }
    void setModelName(const QString& name) { modelName_ = name; }

    QString firmwareVersion() const override { return firmwareVersion_; }
    void setFirmwareVersion(const QString& v) { firmwareVersion_ = v; }

    QString description() const override { return description_; }
    void setDescription(const QString& d) { description_ = d; }

protected:
    bool connected_ = false;
    QString dutId_;
    QString modelName_;
    QString firmwareVersion_;
    QString description_;
};

} // namespace eon::sdk
