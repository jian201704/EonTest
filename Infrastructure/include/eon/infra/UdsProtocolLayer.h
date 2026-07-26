#pragma once

#include <QByteArray>
#include <QObject>
#include <QVariantMap>

#include "eon/sdk/IDriverPlugin.h"

namespace eon::infra {

// ============================================================================
// UdsService — UDS 服务 ID (ISO 14229-1)
// ============================================================================
namespace UdsService {
    constexpr quint8 DiagnosticSessionControl   = 0x10;
    constexpr quint8 EcuReset                   = 0x11;
    constexpr quint8 SecurityAccess             = 0x27;
    constexpr quint8 CommunicationControl       = 0x28;
    constexpr quint8 TesterPresent              = 0x3E;
    constexpr quint8 ReadDataByIdentifier       = 0x22;
    constexpr quint8 ReadMemoryByAddress        = 0x23;
    constexpr quint8 WriteDataByIdentifier      = 0x2E;
    constexpr quint8 WriteMemoryByAddress       = 0x3D;
    constexpr quint8 RoutineControl             = 0x31;
    constexpr quint8 RequestDownload            = 0x34;
    constexpr quint8 RequestUpload              = 0x35;
    constexpr quint8 TransferData               = 0x36;
    constexpr quint8 RequestTransferExit        = 0x37;
    constexpr quint8 ClearDiagnosticInformation = 0x14;
    constexpr quint8 ReadDtcInformation         = 0x19;
    constexpr quint8 InputOutputControlById     = 0x2F;
    constexpr quint8 ControlDtcSetting          = 0x85;
}

// ============================================================================
// UdsResponseCode — UDS 否定响应码 (ISO 14229-1 Annex A)
// ============================================================================
namespace UdsNrc {
    constexpr quint8 GeneralReject             = 0x10;
    constexpr quint8 ServiceNotSupported       = 0x11;
    constexpr quint8 SubFunctionNotSupported   = 0x12;
    constexpr quint8 IncorrectMessageLength    = 0x13;
    constexpr quint8 ConditionsNotCorrect      = 0x22;
    constexpr quint8 RequestSequenceError      = 0x24;
    constexpr quint8 RequestOutOfRange         = 0x31;
    constexpr quint8 SecurityAccessDenied      = 0x33;
    constexpr quint8 InvalidKey                = 0x35;
    constexpr quint8 ExceedNumberOfAttempts    = 0x36;
    constexpr quint8 RequiredTimeDelayNotExpired = 0x37;
    constexpr quint8 UploadDownloadNotAccepted = 0x70;
    constexpr quint8 TransferDataSuspended     = 0x71;
    constexpr quint8 GeneralProgrammingFailure = 0x72;
    constexpr quint8 WrongBlockSequenceCounter = 0x73;
    constexpr quint8 ResponsePending           = 0x78;
    constexpr quint8 ServiceNotSupportedInActiveSession = 0x7F;
}

// ============================================================================
// UdsProtocolLayer — ISO 14229 UDS 诊断协议实现
// 基于 CAN 总线 (ISO 15765-2 DoCAN) 或 CAN FD
// ============================================================================
class UdsProtocolLayer : public QObject, public eon::sdk::IProtocolLayer {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IPROTOCOLLAYER_IID FILE "uds.json")
    Q_INTERFACES(eon::sdk::IProtocolLayer)

public:
    explicit UdsProtocolLayer(QObject* parent = nullptr);
    ~UdsProtocolLayer() override;

    // --- IDriverPlugin ---
    QString id() const override { return "eon.protocol.uds"; }
    QString displayName() const override { return "UDS Protocol (ISO 14229)"; }
    QString version() const override { return "1.0.0"; }
    QVariantMap capabilities() const override;
    bool initialize(QString& errorMessage) override;
    void shutdown() override;
    bool isInitialized() const override { return initialized_; }

    // --- IProtocolLayer ---
    bool bindBus(eon::sdk::IBusDriver* busDriver, QString& errorMessage) override;
    void unbindBus() override;
    bool isReady() const override;
    QString protocolName() const override { return "UDS"; }
    QString protocolVersion() const override { return "ISO 14229-1:2020"; }

    bool sendRequest(const QByteArray& request, QByteArray& response,
                     int timeoutMs, QString& errorMessage) override;
    bool sendRequestEx(const eon::sdk::BusFrame& requestFrame,
                       eon::sdk::BusFrame& responseFrame,
                       int timeoutMs, QString& errorMessage) override;

    // --- UDS 高层便捷接口 ---

    /// 切换诊断会话 (0x10)
    bool diagnosticSessionControl(quint8 sessionType, int timeoutMs, QString& errorMessage);

    /// ECU 复位 (0x11)
    bool ecuReset(quint8 resetType, int timeoutMs, QString& errorMessage);

    /// 安全访问 - 请求种子 (0x27 01)
    bool securityAccessRequestSeed(quint8 level, QByteArray& seed,
                                   int timeoutMs, QString& errorMessage);

    /// 安全访问 - 发送密钥 (0x27 02)
    bool securityAccessSendKey(quint8 level, const QByteArray& key,
                               int timeoutMs, QString& errorMessage);

    /// 读取数据 (0x22)
    bool readDataByIdentifier(quint16 did, QByteArray& data,
                              int timeoutMs, QString& errorMessage);

    /// 写入数据 (0x2E)
    bool writeDataByIdentifier(quint16 did, const QByteArray& data,
                               int timeoutMs, QString& errorMessage);

    /// Tester Present (0x3E)
    bool testerPresent(quint8 subFunction, int timeoutMs, QString& errorMessage);

    /// 读取 DTC (0x19)
    bool readDtcByStatusMask(quint8 statusMask, QVariantList& dtcs,
                             int timeoutMs, QString& errorMessage);

    /// 例程控制 (0x31)
    bool routineControl(quint8 controlType, quint16 routineId,
                        const QByteArray& params, QByteArray& result,
                        int timeoutMs, QString& errorMessage);

    /// 设置诊断 CAN ID
    void setCanIds(quint32 requestId, quint32 responseId, quint32 functionalId = 0);

    /// 是否扩展 CAN ID (29-bit)
    void setExtendedAddressing(bool extended);

private:
    /// 发送 UDS 请求并解析响应
    bool sendUdsRequest(const QByteArray& request, QByteArray& response,
                        int timeoutMs, QString& errorMessage);

    /// 解析否定响应
    bool isNegativeResponse(const QByteArray& response, quint8* nrc = nullptr) const;

    /// 构建 CAN 帧
    eon::sdk::BusFrame buildCanFrame(quint32 canId, const QByteArray& data) const;

    /// 将 UDS 数据编码为 ISO-TP 单帧
    QByteArray encodeSingleFrame(const QByteArray& data) const;

    /// 解析 ISO-TP 单帧响应
    bool decodeSingleFrame(const QByteArray& frame, QByteArray& data) const;

    bool initialized_ = false;
    eon::sdk::IBusDriver* busDriver_ = nullptr;

    // CAN 寻址
    quint32 requestCanId_ = 0x7E0;      // 物理请求 ID (OBD-II 默认)
    quint32 responseCanId_ = 0x7E8;     // 物理响应 ID
    quint32 functionalCanId_ = 0x7DF;   // 功能寻址 ID
    bool extendedCanId_ = false;         // 29-bit CAN ID

    // 统计
    int txCount_ = 0;
    int rxCount_ = 0;
    int nrcCount_ = 0;
    int timeoutCount_ = 0;
};

} // namespace eon::infra
