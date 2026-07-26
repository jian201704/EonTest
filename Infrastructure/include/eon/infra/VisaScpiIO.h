#pragma once

#include "eon/sdk/IScpiIO.h"
#include "eon/sdk/ITransport.h"
#include <QString>

// --- 最小化 VISA 类型定义 (IVI Foundation 标准) ---
typedef unsigned long ViSession;
typedef long          ViStatus;
typedef unsigned long ViUInt32;
typedef ViUInt32*     ViPUInt32;
typedef ViSession*    ViPSession;
typedef unsigned char ViByte;
typedef ViByte*       ViBuf;
typedef char          ViChar;
typedef unsigned long ViAccessMode;
typedef unsigned long ViAttrState;
typedef ViUInt32      ViAttr;

#define VI_SUCCESS       0L
#define VI_NULL          0
#define VI_ATTR_TMO_VALUE  0x3FFF0072L

// --- VISA 函数指针类型 ---
typedef ViStatus (*viOpenDefaultRM_t)(ViPSession);
typedef ViStatus (*viOpen_t)(ViSession, const ViChar*, ViAccessMode, ViUInt32, ViPSession);
typedef ViStatus (*viClose_t)(ViSession);
typedef ViStatus (*viWrite_t)(ViSession, ViBuf, ViUInt32, ViPUInt32);
typedef ViStatus (*viRead_t)(ViSession, ViBuf, ViUInt32, ViPUInt32);
typedef ViStatus (*viClear_t)(ViSession);
typedef ViStatus (*viStatusDesc_t)(ViSession, ViStatus, ViChar[]);
typedef ViStatus (*viSetAttribute_t)(ViSession, ViAttr, ViAttrState);

namespace eon::infra {

/// <summary>
/// VISA SCPI I/O 实现（运行时动态加载 visa32.dll / libvisa.so）。
/// 统一串口、TCP/IP、USB、GPIB 四种物理接口。
/// 资源字符串（OpenTAP 兼容）：
///   "ASRL::COM5::INSTR"           — 串口
///   "TCPIP::192.168.1.1::INSTR"   — LAN
///   "USB::0x1234::0x5678::INSTR"  — USB TMC
///   "GPIB::5::INSTR"              — GPIB
/// </summary>
class VisaScpiIO : public eon::sdk::IScpiIO, public eon::sdk::ITransport {
public:
    VisaScpiIO();
    ~VisaScpiIO() override;

    /// 是否成功加载了 VISA 库
    bool isVisaAvailable() const { return visaLoaded_; }

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
    QString transportName() const override { return QStringLiteral("VISA"); }
    eon::sdk::ITransport* transport() override { return this; }
    const eon::sdk::ITransport* transport() const override { return this; }

private:
    bool loadVisaLibrary();
    QString visaStatusString(ViStatus st) const;
    QString buildResourceString(const QVariantMap& config) const;

    // 函数指针
    viOpenDefaultRM_t  pOpenDefaultRM = nullptr;
    viOpen_t           pOpen          = nullptr;
    viClose_t          pClose         = nullptr;
    viWrite_t          pWrite         = nullptr;
    viRead_t           pRead          = nullptr;
    viClear_t          pClear         = nullptr;
    viStatusDesc_t     pStatusDesc    = nullptr;
    viSetAttribute_t   pSetAttribute  = nullptr;

    bool visaLoaded_ = false;
    ViSession defaultRM_ = VI_NULL;
    ViSession instr_ = VI_NULL;
    QString resourceString_;
    int openTimeoutMs_ = 5000;
    int vioTimeoutMs_ = 5000;
};

} // namespace eon::infra
