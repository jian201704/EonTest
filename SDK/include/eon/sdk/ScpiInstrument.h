#pragma once

#include "eon/sdk/IResource.h"
#include "eon/sdk/IScpiIO.h"
#include "eon/sdk/IStepPlugin.h"
#include "eon/sdk/Verdict.h"
#include <QObject>
#include <QMutex>
#include <QString>
#include <QVariantMap>

namespace eon::sdk {

/// <summary>
/// SCPI 仪器基类（参考 Keysight OpenTAP ScpiInstrument 设计）。
///
/// 封装：
/// - IScpiIO 通信抽象（串口/TCP/VISA 统一）
/// - 自动 *IDN? / *CLS / VIClear 连接初始化
/// - 可配置的 QueryErrorAfterCommand 错误检查
/// - 线程安全的 commandLock
///
/// 继承此类的插件只需实现 executeStep()，通过 scpiQuery() 和 scpiCommand() 发送指令。
/// </summary>
class ScpiInstrument : public QObject, public IResource, public IStepPlugin {
    Q_OBJECT
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    ScpiInstrument();
    virtual ~ScpiInstrument();

    // ============================================================
    // IResource 接口
    // ============================================================
    bool open() override;
    void close() override;
    QString name() const override { return name_; }
    bool isConnected() const override { return io_ && io_->isConnected(); }
    void setName(const QString& n) { name_ = n; }

    // ============================================================
    // IStepPlugin（子类重写）
    // ============================================================
    virtual QString id() const override = 0;
    virtual bool executeStep(WorkflowContext& context, QString& errorMessage) override = 0;

    // ============================================================
    // 连接行为配置（参考 OpenTAP ScpiInstrument 设置）
    // ============================================================

    /// VISA 地址（如 "TCPIP::192.168.1.1::INSTR" / "GPIB::5::INSTR" / "COM5"）
    QString visaAddress() const { return visaAddress_; }
    void setVisaAddress(const QString& addr) { visaAddress_ = addr; }

    /// I/O 超时（ms），默认 2000
    int ioTimeoutMs() const { return ioTimeoutMs_; }
    void setIoTimeoutMs(int ms) { ioTimeoutMs_ = ms; }

    /// 连接时发送 VIClear()（默认 true，对应 OpenTAP SendClearOnConnect）
    bool sendClearOnConnect() const { return sendClearOnConnect_; }
    void setSendClearOnConnect(bool v) { sendClearOnConnect_ = v; }

    /// 连接时发送 *IDN?（默认 true，对应 OpenTAP SendIDNOnConnect）
    bool sendIDNOnConnect() const { return sendIDNOnConnect_; }
    void setSendIDNOnConnect(bool v) { sendIDNOnConnect_ = v; }

    /// 连接时发送 *CLS（默认 true，对应 OpenTAP SendCLSOnConnect）
    bool sendCLSOnConnect() const { return sendCLSOnConnect_; }
    void setSendCLSOnConnect(bool v) { sendCLSOnConnect_ = v; }

    /// 每次命令后自动查询 SYST:ERR?（默认 false，对应 OpenTAP QueryErrorAfterCommand）
    bool queryErrorAfterCommand() const { return queryErrorAfterCommand_; }
    void setQueryErrorAfterCommand(bool v) { queryErrorAfterCommand_ = v; }

    /// 详细 SCPI 日志（默认 true）
    bool verboseLoggingEnabled() const { return verboseLogging_; }
    void setVerboseLoggingEnabled(bool v) { verboseLogging_ = v; }

    /// 仪器标识字符串（*IDN? 响应）
    QString idnString() const { return idnString_; }

    // ============================================================
    // 保护方法
    // ============================================================
protected:
    /// 发送 SCPI 命令（不读响应），可选错误检查
    bool scpiCommand(const QString& cmd, int timeoutMs = 200);

    /// 发送 SCPI 查询，返回响应字符串
    QString scpiQuery(const QString& query, int timeoutMs = 1000);

    /// 子类在构造函数或 open() 中设置 io_
    IScpiIO* io_ = nullptr;

    /// 从 context.data 读取参数（支持回退键名）
    QString param(const QVariantMap& data, const QString& key, const QString& fallback = {}) const;
    int paramInt(const QVariantMap& data, const QString& key, int fallback = 0) const;
    double paramDouble(const QVariantMap& data, const QString& key, double fallback = 0.0) const;

private:
    /// 发送 *ESR? 并检查错误队列
    void checkScpiErrors(const QString& cmdContext);

    /// 发送 *CLS（清除错误队列）
    bool commandCls();

    /// 发送 *IDN? 并返回标识字符串
    QString queryIdn();

    /// 线程锁（对应 OpenTAP commandLock）
    QMutex commandLock_;

    // 配置项
    QString name_;
    QString visaAddress_;
    int ioTimeoutMs_ = 2000;
    bool sendClearOnConnect_ = true;
    bool sendIDNOnConnect_ = true;
    bool sendCLSOnConnect_ = true;
    bool queryErrorAfterCommand_ = false;
    bool verboseLogging_ = true;

    // 状态
    QString idnString_;
};

} // namespace eon::sdk
