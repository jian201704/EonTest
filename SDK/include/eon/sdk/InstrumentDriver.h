#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace eon::sdk {

class IBackend;

// ============================================================================
// InstrumentDriver — 仪器驱动抽象基类
//
// 子类：PowerSupplyDriver, MultimeterDriver, OscilloscopeDriver, ...
//
// 生命周期：
//   1. 构造函数接收 IBackend*（由 InstrumentManager 注入）
//   2. open(config) → 通过 backend 连接仪器
//   3. execute(cmd, params) → 发送指令（不期待返回数据）
//   4. query(cmd, params) → 发送查询指令，返回结果
//   5. close()
//
// 命令命名约定：
//   on/off     — 开关控制
//   set_xxx    — 设置参数（电压、电流、频段等）
//   measure    — 测量
//   identify   — *IDN? 仪器识别
// ============================================================================
class InstrumentDriver : public QObject {
    Q_OBJECT
public:
    explicit InstrumentDriver(QObject* parent = nullptr) : QObject(parent) {}
    ~InstrumentDriver() override = default;

    /// 驱动类型标识，如 "power.supply", "measure.voltage"
    virtual QString driverType() const = 0;

    /// 通过后端打开连接
    virtual bool open(IBackend* backend, const QVariantMap& config, QString& errorMessage) = 0;

    /// 关闭连接
    virtual void close() = 0;

    /// 是否已连接
    virtual bool isOpen() const = 0;

    /// 执行命令（不期待返回值），成功返回 true
    virtual bool execute(const QString& command, const QVariantMap& params, QString& errorMessage) = 0;

    /// 执行查询命令，返回结果 QVariantMap
    virtual QVariantMap query(const QString& command, const QVariantMap& params, QString& errorMessage) = 0;

    /// 获取仪器标识（*IDN? 结果）
    virtual QString identity() const = 0;

    /// 获取 SCPI 指令追踪记录
    virtual QString scpiTrace() const = 0;
};

} // namespace eon::sdk
