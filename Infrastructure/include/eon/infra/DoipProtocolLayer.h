#pragma once

#include <QByteArray>
#include <QObject>
#include <QVariantMap>
#include <QTimer>

#include "eon/sdk/IDriverPlugin.h"

namespace eon::infra {

// ============================================================================
// DoIP 常量 (ISO 13400-2)
// ============================================================================
namespace Doip {

// DoIP 协议版本
constexpr quint8 ProtocolVersion          = 0x02;  // ISO 13400-2:2019
constexpr quint8 InverseProtocolVersion   = 0xFD;

// DoIP 通用 Header (4 字节)
// Byte 0: Protocol Version
// Byte 1: Inverse Protocol Version  
// Byte 2-3: Payload Type

// Payload Type 枚举
namespace PayloadType {
    constexpr quint16 GenericNegativeAck        = 0x0000;
    constexpr quint16 VehicleIdRequest          = 0x0001;
    constexpr quint16 VehicleIdRequestWithEid   = 0x0002;
    constexpr quint16 VehicleIdRequestWithVin   = 0x0003;
    constexpr quint16 VehicleAnnouncement       = 0x0004;
    constexpr quint16 RoutingActivationRequest  = 0x0005;
    constexpr quint16 RoutingActivationResponse = 0x0006;
    constexpr quint16 AliveCheckRequest         = 0x0007;
    constexpr quint16 AliveCheckResponse        = 0x0008;
    constexpr quint16 DoipEntityStatusRequest   = 0x4001;
    constexpr quint16 DoipEntityStatusResponse  = 0x4002;
    constexpr quint16 DiagnosticPowerModeRequest = 0x4003;
    constexpr quint16 DiagnosticPowerModeResponse = 0x4004;
    constexpr quint16 DiagnosticMessage         = 0x8001;
    constexpr quint16 DiagnosticMessagePositiveAck = 0x8002;
    constexpr quint16 DiagnosticMessageNegativeAck = 0x8003;
}

// Routing Activation Type
namespace ActivationType {
    constexpr quint8 Default        = 0x00;
    constexpr quint8 WwhObd         = 0x01;
    constexpr quint8 CentralSecurity = 0xE0;
}

// Routing Activation Response Code
namespace ActivationResponse {
    constexpr quint8 Successful             = 0x10;
    constexpr quint8 NoSocketAvailable      = 0x11;
    constexpr quint8 InvalidSourceAddress   = 0x12;
    constexpr quint8 AlreadyActive          = 0x13;
    constexpr quint8 AuthenticationMissing  = 0x14;
    constexpr quint8 AuthenticationRejected = 0x15;
    constexpr quint8 UnsupportedActType     = 0x16;
    constexpr quint8 TlsRequired            = 0x17;
}

// Generic NACK Code
namespace NackCode {
    constexpr quint8 IncorrectPattern = 0x00;
    constexpr quint8 UnknownPayload   = 0x01;
    constexpr quint8 MessageTooLarge  = 0x02;
    constexpr quint8 OutOfMemory      = 0x03;
    constexpr quint8 InvalidTarget    = 0x04;
}

// DoIP UDP Discovery Port
constexpr quint16 UdpDiscoveryPort = 13400;

// DoIP TCP Data Port
constexpr quint16 TcpDataPort = 13400;

// DoIP 多播地址
inline QString multicastAddress() { return "224.0.23.101"; }

// DoIP Header 大小
constexpr int HeaderSize = 8;  // 通用 8 字节 header

} // namespace Doip

// ============================================================================
// DoipVehicleInfo — 车辆信息（从 Vehicle Announcement 解析）
// ============================================================================
struct DoipVehicleInfo {
    QByteArray vin;           // 17 位 VIN
    quint16 logicalAddress = 0;
    QByteArray eid;           // 6 字节 Entity ID
    QByteArray gid;           // 6 字节 Group ID
    quint8 furtherAction = 0;
    qint64 lastSeenMs = 0;

    bool isValid() const {
        return !vin.isEmpty() || !eid.isEmpty();
    }
};

// ============================================================================
// DoipProtocolLayer — ISO 13400 DoIP 协议实现
//
// 基于 TcpBusDriver，提供:
//   - UDP 车辆发现 (vehicle discovery)
//   - TCP 路由激活 (routing activation)
//   - UDS 消息封装/解封 (diagnostic message)
//   - Alive check 保活
// ============================================================================
class DoipProtocolLayer : public QObject, public eon::sdk::IProtocolLayer {
public:
    explicit DoipProtocolLayer(QObject* parent = nullptr);
    ~DoipProtocolLayer() override;

    // --- IDriverPlugin ---
    QString id() const override { return "eon.protocol.doip"; }
    QString displayName() const override { return "DoIP Protocol (ISO 13400)"; }
    QString version() const override { return "1.0.0"; }
    QVariantMap capabilities() const override;
    bool initialize(QString& errorMessage) override;
    void shutdown() override;
    bool isInitialized() const override { return initialized_; }

    // --- IProtocolLayer ---
    bool bindBus(eon::sdk::IBusDriver* busDriver, QString& errorMessage) override;
    void unbindBus() override;
    bool isReady() const override;
    QString protocolName() const override { return "DoIP"; }
    QString protocolVersion() const override { return "ISO 13400-2:2019"; }

    bool sendRequest(const QByteArray& request, QByteArray& response,
                     int timeoutMs, QString& errorMessage) override;
    bool sendRequestEx(const eon::sdk::BusFrame& requestFrame,
                       eon::sdk::BusFrame& responseFrame,
                       int timeoutMs, QString& errorMessage) override;

    // --- DoIP 车辆发现 ---

    /// UDP 多播发送 Vehicle Identification Request（广播模式）
    bool discoverVehicles(QList<DoipVehicleInfo>& vehicles, int timeoutMs,
                          QString& errorMessage);

    /// UDP 单播请求指定 VIN 的车辆
    bool requestVehicleByVin(const QString& vin, DoipVehicleInfo& info,
                             int timeoutMs, QString& errorMessage);

    /// UDP 单播请求指定 EID 的车辆
    bool requestVehicleByEid(const QByteArray& eid, DoipVehicleInfo& info,
                             int timeoutMs, QString& errorMessage);

    /// 等待 Vehicle Announcement（被动词汇）
    bool waitForAnnouncement(DoipVehicleInfo& info, int timeoutMs,
                             QString& errorMessage);

    // --- DoIP 路由激活 ---

    /// TCP 路由激活（连接后必须调用）
    /// logicalAddress: 测试仪逻辑地址（通常 0x0E00-0x0FFF）
    /// activationType: 激活类型 (Default=0x00)
    bool activateRouting(quint16 logicalAddress, quint8 activationType,
                         int timeoutMs, QString& errorMessage);

    /// 路由是否已激活
    bool isRoutingActive() const { return routingActive_; }

    // --- DoIP 诊断消息 ---

    /// 发送 UDS 请求并接收 UDS 响应（封装 DoIP diagnostic message）
    /// sourceAddr: 源逻辑地址 (测试仪)
    /// targetAddr: 目标逻辑地址 (ECU)
    bool sendDiagnosticMessage(quint16 sourceAddr, quint16 targetAddr,
                               const QByteArray& udsPayload,
                               QByteArray& udsResponse,
                               int timeoutMs, QString& errorMessage);

    // --- Alive Check ---

    /// 发送 Alive Check 请求
    bool sendAliveCheck(int timeoutMs, QString& errorMessage);

    // --- 配置 ---

    /// 设置逻辑地址
    void setLogicalAddress(quint16 addr) { logicalAddress_ = addr; }

    /// 设置 EID
    void setEid(const QByteArray& eid) { eid_ = eid; }

    /// 获取发现的车辆列表
    QList<DoipVehicleInfo> discoveredVehicles() const { return discoveredVehicles_; }

    /// 最近一次 TCP 发送/接收的完整 DoIP 帧（用于日志和调试）
    QString lastTxHex() const { return lastTx_.toHex(' ').toUpper(); }
    QString lastRxHex() const { return lastRx_.toHex(' ').toUpper(); }

private:
    // DoIP 帧构建
    QByteArray buildDoipHeader(quint16 payloadType, quint32 payloadLength) const;
    QByteArray buildVehicleIdRequest() const;
    QByteArray buildVehicleIdRequestWithVin(const QString& vin) const;
    QByteArray buildVehicleIdRequestWithEid(const QByteArray& eid) const;
    QByteArray buildRoutingActivationRequest(quint16 sourceAddr, quint8 actType) const;
    QByteArray buildDiagnosticMessage(quint16 sourceAddr, quint16 targetAddr,
                                      const QByteArray& udsPayload) const;
    QByteArray buildAliveCheckRequest() const;

    // DoIP 响应解析
    bool parseDoipHeader(const QByteArray& data, quint16& payloadType,
                         quint32& payloadLength, int& headerEnd) const;
    bool parseVehicleAnnouncement(const QByteArray& payload, DoipVehicleInfo& info) const;
    bool parseRoutingActivationResponse(const QByteArray& payload,
                                        quint8& responseCode, QString& errorMessage) const;
    bool parseDiagnosticMessageResponse(const QByteArray& payload,
                                        QByteArray& udsResponse) const;

    // UDP 收发
    bool sendUdpRequest(const QByteArray& request, const QString& host, quint16 port,
                        int timeoutMs, QString& errorMessage);
    bool receiveUdpResponse(QByteArray& response, QString& sourceHost, quint16& sourcePort,
                            int timeoutMs, QString& errorMessage);

    // TCP 收发（通过绑定的 TcpBusDriver）
    bool sendTcpFrame(const QByteArray& data, QString& errorMessage);
    bool receiveTcpFrame(QByteArray& data, int timeoutMs, QString& errorMessage);

    bool initialized_ = false;
    eon::sdk::IBusDriver* busDriver_ = nullptr;
    bool routingActive_ = false;
    quint16 logicalAddress_ = 0x0E80;  // 默认测试仪逻辑地址
    QByteArray eid_;
    QList<DoipVehicleInfo> discoveredVehicles_;
    // TCP 允许半包和粘包；保存尚未组成完整 DoIP 帧的字节。
    QByteArray rxBuffer_;
    QByteArray lastTx_;
    QByteArray lastRx_;
};

} // namespace eon::infra
