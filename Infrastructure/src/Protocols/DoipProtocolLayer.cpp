#include "eon/infra/DoipProtocolLayer.h"
#include "eon/infra/TcpBusDriver.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>

namespace eon::infra {

DoipProtocolLayer::DoipProtocolLayer(QObject* parent)
    : QObject(parent) {}

DoipProtocolLayer::~DoipProtocolLayer() {
    shutdown();
}

QVariantMap DoipProtocolLayer::capabilities() const {
    return {
        {"protocol", "DoIP"},
        {"standard", "ISO 13400-2:2019"},
        {"transport", "TCP/IP + UDP"},
        {"udpDiscoveryPort", Doip::UdpDiscoveryPort},
        {"tcpDataPort", Doip::TcpDataPort},
        {"supportsVinDiscovery", true},
        {"supportsEidDiscovery", true},
        {"supportsRoutingActivation", true},
        {"maxDiagnosticPayload", 65535}
    };
}

bool DoipProtocolLayer::initialize(QString& errorMessage) {
    Q_UNUSED(errorMessage)
    if (initialized_) return true;
    initialized_ = true;
    return true;
}

void DoipProtocolLayer::shutdown() {
    unbindBus();
    initialized_ = false;
    discoveredVehicles_.clear();
    rxBuffer_.clear();
}

bool DoipProtocolLayer::bindBus(eon::sdk::IBusDriver* busDriver, QString& errorMessage) {
    if (!initialized_) {
        errorMessage = "DoIP protocol layer not initialized.";
        return false;
    }
    if (!busDriver) {
        errorMessage = "Bus driver is null.";
        return false;
    }
    if (!busDriver->isOpen()) {
        errorMessage = "Bus driver is not open.";
        return false;
    }

    const auto type = busDriver->busType();
    if (type != eon::sdk::BusType::TCP && type != eon::sdk::BusType::Virtual) {
        errorMessage = "DoIP protocol requires TCP or Virtual bus type.";
        return false;
    }

    busDriver_ = busDriver;
    return true;
}

void DoipProtocolLayer::unbindBus() {
    busDriver_ = nullptr;
    routingActive_ = false;
}

bool DoipProtocolLayer::isReady() const {
    return initialized_ && busDriver_ && busDriver_->isOpen() && routingActive_;
}

// ============================================================================
// 底层收发
// ============================================================================

bool DoipProtocolLayer::sendRequest(const QByteArray& request, QByteArray& response,
                                     int timeoutMs, QString& errorMessage) {
    // 默认使用 diagnostic message 发送（需已路由激活）
    QByteArray udsResponse;
    if (!sendDiagnosticMessage(logicalAddress_, 0x0000, request,
                                udsResponse, timeoutMs, errorMessage)) {
        return false;
    }
    response = udsResponse;
    return true;
}

bool DoipProtocolLayer::sendRequestEx(const eon::sdk::BusFrame& requestFrame,
                                       eon::sdk::BusFrame& responseFrame,
                                       int timeoutMs, QString& errorMessage) {
    // 在 DoIP 中，使用 TCP 发送 raw 帧
    QByteArray tcpResponse;
    if (!sendTcpFrame(requestFrame.data, errorMessage)) {
        return false;
    }
    if (!receiveTcpFrame(tcpResponse, timeoutMs, errorMessage)) {
        return false;
    }
    responseFrame.data = tcpResponse;
    responseFrame.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    return true;
}

// ============================================================================
// 车辆发现
// ============================================================================

bool DoipProtocolLayer::discoverVehicles(QList<DoipVehicleInfo>& vehicles,
                                          int timeoutMs, QString& errorMessage) {
    discoveredVehicles_.clear();

    // 发送 UDP 多播 Vehicle Identification Request
    const QByteArray request = buildVehicleIdRequest();
    const QString mcastAddr = Doip::multicastAddress();

    if (!sendUdpRequest(request, mcastAddr, Doip::UdpDiscoveryPort, timeoutMs, errorMessage)) {
        return false;
    }

    // 收集响应（可能有多个车辆）
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        QByteArray response;
        QString sourceHost;
        quint16 sourcePort = 0;
        const int remainingMs = static_cast<int>(deadline - QDateTime::currentMSecsSinceEpoch());
        if (remainingMs <= 0) break;

        if (!receiveUdpResponse(response, sourceHost, sourcePort, remainingMs, errorMessage)) {
            break; // timeout or error
        }

        quint16 payloadType = 0;
        quint32 payloadLength = 0;
        int headerEnd = 0;
        if (parseDoipHeader(response, payloadType, payloadLength, headerEnd)) {
            if (payloadType == Doip::PayloadType::VehicleAnnouncement) {
                DoipVehicleInfo info;
                info.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
                if (parseVehicleAnnouncement(response.mid(headerEnd), info)) {
                    discoveredVehicles_.append(info);
                }
            }
        }
    }

    vehicles = discoveredVehicles_;
    return !vehicles.isEmpty();
}

bool DoipProtocolLayer::requestVehicleByVin(const QString& vin, DoipVehicleInfo& info,
                                             int timeoutMs, QString& errorMessage) {
    // 单播到已知车辆，或广播 VIN 查询
    // 这里使用 UDP 单播到已发现的车辆
    if (discoveredVehicles_.isEmpty()) {
        // 先广播发现
        QList<DoipVehicleInfo> vehicles;
        if (!discoverVehicles(vehicles, 2000, errorMessage)) {
            return false;
        }
    }

    const QByteArray request = buildVehicleIdRequestWithVin(vin);

    // 发送到多播地址
    if (!sendUdpRequest(request, Doip::multicastAddress(), Doip::UdpDiscoveryPort,
                        timeoutMs, errorMessage)) {
        return false;
    }

    // 等待响应
    QByteArray response;
    QString sourceHost;
    quint16 sourcePort = 0;
    if (!receiveUdpResponse(response, sourceHost, sourcePort, timeoutMs, errorMessage)) {
        return false;
    }

    quint16 payloadType = 0;
    quint32 payloadLength = 0;
    int headerEnd = 0;
    if (!parseDoipHeader(response, payloadType, payloadLength, headerEnd)) {
        errorMessage = "Invalid DoIP response.";
        return false;
    }

    if (payloadType == Doip::PayloadType::VehicleAnnouncement) {
        info.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
        return parseVehicleAnnouncement(response.mid(headerEnd), info);
    }

    errorMessage = "Unexpected payload type.";
    return false;
}

bool DoipProtocolLayer::requestVehicleByEid(const QByteArray& eid,
                                             DoipVehicleInfo& info,
                                             int timeoutMs, QString& errorMessage) {
    const QByteArray request = buildVehicleIdRequestWithEid(eid);

    if (!sendUdpRequest(request, Doip::multicastAddress(), Doip::UdpDiscoveryPort,
                        timeoutMs, errorMessage)) {
        return false;
    }

    QByteArray response;
    QString sourceHost;
    quint16 sourcePort = 0;
    if (!receiveUdpResponse(response, sourceHost, sourcePort, timeoutMs, errorMessage)) {
        return false;
    }

    quint16 payloadType = 0;
    quint32 payloadLength = 0;
    int headerEnd = 0;
    if (!parseDoipHeader(response, payloadType, payloadLength, headerEnd)) {
        errorMessage = "Invalid DoIP response.";
        return false;
    }

    if (payloadType == Doip::PayloadType::VehicleAnnouncement) {
        info.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
        return parseVehicleAnnouncement(response.mid(headerEnd), info);
    }

    errorMessage = "Unexpected payload type.";
    return false;
}

bool DoipProtocolLayer::waitForAnnouncement(DoipVehicleInfo& info, int timeoutMs,
                                             QString& errorMessage) {
    QByteArray response;
    QString sourceHost;
    quint16 sourcePort = 0;

    if (!receiveUdpResponse(response, sourceHost, sourcePort, timeoutMs, errorMessage)) {
        return false;
    }

    quint16 payloadType = 0;
    quint32 payloadLength = 0;
    int headerEnd = 0;
    if (!parseDoipHeader(response, payloadType, payloadLength, headerEnd)) {
        errorMessage = "Invalid DoIP announcement.";
        return false;
    }

    if (payloadType != Doip::PayloadType::VehicleAnnouncement) {
        errorMessage = "Expected vehicle announcement.";
        return false;
    }

    info.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
    return parseVehicleAnnouncement(response.mid(headerEnd), info);
}

// ============================================================================
// 路由激活
// ============================================================================

bool DoipProtocolLayer::activateRouting(quint16 logicalAddress, quint8 activationType,
                                         int timeoutMs, QString& errorMessage) {
    if (!busDriver_ || !busDriver_->isOpen()) {
        errorMessage = "Bus driver is not open.";
        return false;
    }

    logicalAddress_ = logicalAddress;

    const QByteArray request = buildRoutingActivationRequest(logicalAddress, activationType);

    if (!sendTcpFrame(request, errorMessage)) {
        return false;
    }

    QByteArray response;
    if (!receiveTcpFrame(response, timeoutMs, errorMessage)) {
        return false;
    }

    quint16 payloadType = 0;
    quint32 payloadLength = 0;
    int headerEnd = 0;
    if (!parseDoipHeader(response, payloadType, payloadLength, headerEnd)) {
        errorMessage = "Invalid routing activation response.";
        return false;
    }

    if (payloadType != Doip::PayloadType::RoutingActivationResponse) {
        errorMessage = "Expected routing activation response.";
        return false;
    }

    quint8 responseCode = 0;
    if (!parseRoutingActivationResponse(response.mid(headerEnd), responseCode, errorMessage)) {
        return false;
    }

    if (responseCode != Doip::ActivationResponse::Successful) {
        errorMessage = QString("Routing activation denied: 0x%1")
                           .arg(responseCode, 2, 16, QChar('0')).toUpper();
        return false;
    }

    routingActive_ = true;
    return true;
}

// ============================================================================
// 诊断消息
// ============================================================================

bool DoipProtocolLayer::sendDiagnosticMessage(quint16 sourceAddr, quint16 targetAddr,
                                               const QByteArray& udsPayload,
                                               QByteArray& udsResponse,
                                               int timeoutMs, QString& errorMessage) {
    if (!routingActive_) {
        errorMessage = "DoIP routing not activated.";
        return false;
    }

    const QByteArray diagMsg = buildDiagnosticMessage(sourceAddr, targetAddr, udsPayload);

    if (!sendTcpFrame(diagMsg, errorMessage)) {
        return false;
    }

    // ECU 可能先返回一个或多个诊断 ACK/NACK，再返回最终 0x8001 响应。
    // 必须持续读取并忽略成功的中间确认，不能把中间帧当作最终结果。
    QElapsedTimer waitTimer;
    waitTimer.start();
    while (true) {
        const int remainingMs = timeoutMs < 0 ? timeoutMs : qMax(1, timeoutMs - static_cast<int>(waitTimer.elapsed()));
        QByteArray response;
        if (!receiveTcpFrame(response, remainingMs, errorMessage)) return false;

        quint16 payloadType = 0;
        quint32 payloadLength = 0;
        int headerEnd = 0;
        if (!parseDoipHeader(response, payloadType, payloadLength, headerEnd)) {
            errorMessage = "Invalid DoIP diagnostic response.";
            return false;
        }

        const QByteArray payload = response.mid(headerEnd);
        if (payloadType == Doip::PayloadType::DiagnosticMessage) {
            QByteArray candidate;
            if (!parseDiagnosticMessageResponse(payload, candidate)) {
                errorMessage = "Invalid DoIP diagnostic response payload.";
                return false;
            }
            // UDS NRC 0x78 (Response Pending)：ECU 已收到请求，但还未完成，
            // 继续等待同一 TCP/DoIP 会话中的最终响应。
            if (candidate.size() >= 3 && udsPayload.size() >= 1 &&
                static_cast<quint8>(candidate.at(0)) == 0x7F &&
                static_cast<quint8>(candidate.at(1)) ==
                    static_cast<quint8>(udsPayload.at(0)) &&
                static_cast<quint8>(candidate.at(2)) == 0x78) {
                if (timeoutMs >= 0 && waitTimer.elapsed() >= timeoutMs) {
                    errorMessage = "Timed out waiting after UDS Response Pending (0x78).";
                    return false;
                }
                continue;
            }
            udsResponse = candidate;
            return true;
        }

        if (payloadType == Doip::PayloadType::DiagnosticMessagePositiveAck ||
            payloadType == Doip::PayloadType::DiagnosticMessageNegativeAck) {
            // ACK/NACK payload: TargetAddress(2) + SourceAddress(2) + code(1)
            // + optionally acknowledged UDS service byte.
            if (payload.size() < 5) {
                errorMessage = "Malformed DoIP diagnostic ACK/NACK.";
                return false;
            }
            const quint8 code = static_cast<quint8>(payload.at(4));
            if (payloadType == Doip::PayloadType::DiagnosticMessageNegativeAck && code != 0x00) {
                errorMessage = QString("DoIP diagnostic NACK: 0x%1")
                                   .arg(code, 2, 16, QChar('0')).toUpper();
                return false;
            }
            if (timeoutMs >= 0 && waitTimer.elapsed() >= timeoutMs) {
                errorMessage = "Timed out waiting for final DoIP diagnostic response.";
                return false;
            }
            continue;
        }

        errorMessage = "Unexpected DoIP response type.";
        return false;
    }
}

// ============================================================================
// Alive Check
// ============================================================================

bool DoipProtocolLayer::sendAliveCheck(int timeoutMs, QString& errorMessage) {
    if (!routingActive_) {
        errorMessage = "DoIP routing not activated.";
        return false;
    }

    const QByteArray request = buildAliveCheckRequest();
    if (!sendTcpFrame(request, errorMessage)) {
        return false;
    }

    QByteArray response;
    if (!receiveTcpFrame(response, timeoutMs, errorMessage)) {
        return false;
    }

    quint16 payloadType = 0;
    quint32 payloadLength = 0;
    int headerEnd = 0;
    if (!parseDoipHeader(response, payloadType, payloadLength, headerEnd)) {
        errorMessage = "Invalid alive check response.";
        return false;
    }

    if (payloadType != Doip::PayloadType::AliveCheckResponse) {
        errorMessage = "Expected alive check response.";
        return false;
    }

    return true;
}

// ============================================================================
// DoIP 帧构建
// ============================================================================

QByteArray DoipProtocolLayer::buildDoipHeader(quint16 payloadType, quint32 payloadLength) const {
    QByteArray header(Doip::HeaderSize, '\0');
    header[0] = static_cast<char>(Doip::ProtocolVersion);
    header[1] = static_cast<char>(Doip::InverseProtocolVersion);
    header[2] = static_cast<char>((payloadType >> 8) & 0xFF);
    header[3] = static_cast<char>(payloadType & 0xFF);
    header[4] = static_cast<char>((payloadLength >> 24) & 0xFF);
    header[5] = static_cast<char>((payloadLength >> 16) & 0xFF);
    header[6] = static_cast<char>((payloadLength >> 8) & 0xFF);
    header[7] = static_cast<char>(payloadLength & 0xFF);
    return header;
}

QByteArray DoipProtocolLayer::buildVehicleIdRequest() const {
    // Payload Type 0x0001: 无额外 payload
    return buildDoipHeader(Doip::PayloadType::VehicleIdRequest, 0);
}

QByteArray DoipProtocolLayer::buildVehicleIdRequestWithVin(const QString& vin) const {
    // Payload Type 0x0003: VIN (17 bytes)
    QByteArray payload = vin.toLatin1().leftJustified(17, '\0', true);
    QByteArray frame = buildDoipHeader(Doip::PayloadType::VehicleIdRequestWithVin,
                                       static_cast<quint32>(payload.size()));
    frame.append(payload);
    return frame;
}

QByteArray DoipProtocolLayer::buildVehicleIdRequestWithEid(const QByteArray& eid) const {
    // Payload Type 0x0002: EID (6 bytes)
    QByteArray payload = eid.leftJustified(6, '\0', true);
    QByteArray frame = buildDoipHeader(Doip::PayloadType::VehicleIdRequestWithEid,
                                       static_cast<quint32>(payload.size()));
    frame.append(payload);
    return frame;
}

QByteArray DoipProtocolLayer::buildRoutingActivationRequest(quint16 sourceAddr,
                                                             quint8 actType) const {
    // Payload: SourceAddress(2) + ActivationType(1) + Reserved(4) = 7 bytes
    // ISO 13400-2:2019 可能有 OEM-specific 字段，这里使用最小格式
    QByteArray payload(7, '\0');
    payload[0] = static_cast<char>((sourceAddr >> 8) & 0xFF);
    payload[1] = static_cast<char>(sourceAddr & 0xFF);
    payload[2] = static_cast<char>(actType);
    // bytes 3-6: reserved (0x00000000)

    QByteArray frame = buildDoipHeader(Doip::PayloadType::RoutingActivationRequest,
                                       static_cast<quint32>(payload.size()));
    frame.append(payload);
    return frame;
}

QByteArray DoipProtocolLayer::buildDiagnosticMessage(quint16 sourceAddr, quint16 targetAddr,
                                                      const QByteArray& udsPayload) const {
    // Payload: SourceAddress(2) + TargetAddress(2) + UDS data
    QByteArray payload;
    payload.append(static_cast<char>((sourceAddr >> 8) & 0xFF));
    payload.append(static_cast<char>(sourceAddr & 0xFF));
    payload.append(static_cast<char>((targetAddr >> 8) & 0xFF));
    payload.append(static_cast<char>(targetAddr & 0xFF));
    payload.append(udsPayload);

    QByteArray frame = buildDoipHeader(Doip::PayloadType::DiagnosticMessage,
                                       static_cast<quint32>(payload.size()));
    frame.append(payload);
    return frame;
}

QByteArray DoipProtocolLayer::buildAliveCheckRequest() const {
    // Alive Check Request: 无 payload
    return buildDoipHeader(Doip::PayloadType::AliveCheckRequest, 0);
}

// ============================================================================
// DoIP 响应解析
// ============================================================================

bool DoipProtocolLayer::parseDoipHeader(const QByteArray& data, quint16& payloadType,
                                         quint32& payloadLength, int& headerEnd) const {
    if (data.size() < Doip::HeaderSize) {
        return false;
    }

    const quint8 version = static_cast<quint8>(data.at(0));
    const quint8 inverseVersion = static_cast<quint8>(data.at(1));

    // 校验版本
    if (version != Doip::ProtocolVersion || inverseVersion != Doip::InverseProtocolVersion) {
        return false;
    }

    payloadType = static_cast<quint16>(
        (static_cast<quint8>(data.at(2)) << 8) | static_cast<quint8>(data.at(3)));

    payloadLength = static_cast<quint32>(
        (static_cast<quint8>(data.at(4)) << 24) |
        (static_cast<quint8>(data.at(5)) << 16) |
        (static_cast<quint8>(data.at(6)) << 8) |
        static_cast<quint8>(data.at(7)));

    headerEnd = Doip::HeaderSize;
    return true;
}

bool DoipProtocolLayer::parseVehicleAnnouncement(const QByteArray& payload,
                                                  DoipVehicleInfo& info) const {
    // Vehicle Announcement payload:
    // VIN(17) + LogicalAddress(2) + EID(6) + GID(6) + FurtherAction(1)
    // + VIN/GID sync status(1) = 33 bytes (minimum)
    if (payload.size() < 33) {
        return false;
    }

    info.vin = QByteArray(payload.constData(), 17).trimmed();
    info.logicalAddress = static_cast<quint16>(
        (static_cast<quint8>(payload.at(17)) << 8) | static_cast<quint8>(payload.at(18)));
    info.eid = payload.mid(19, 6);
    info.gid = payload.mid(25, 6);
    info.furtherAction = static_cast<quint8>(payload.at(31));

    return true;
}

bool DoipProtocolLayer::parseRoutingActivationResponse(const QByteArray& payload,
                                                        quint8& responseCode,
                                                        QString& errorMessage) const {
    // Response: TesterLogicalAddr(2) + EntityLogicalAddr(2) + ResponseCode(1) + Reserved(4) = 9 bytes
    if (payload.size() < 9) {
        errorMessage = "Routing activation response too short.";
        return false;
    }

    // testerAddr = (payload[0] << 8) | payload[1];
    // entityAddr = (payload[2] << 8) | payload[3];
    responseCode = static_cast<quint8>(payload.at(4));

    return true;
}

bool DoipProtocolLayer::parseDiagnosticMessageResponse(const QByteArray& payload,
                                                        QByteArray& udsResponse) const {
    // Response: SourceAddr(2) + TargetAddr(2) + UDS response
    if (payload.size() < 5) {
        return false;
    }
    udsResponse = payload.mid(4); // skip source + target addresses
    return true;
}

// ============================================================================
// UDP 收发
// ============================================================================

bool DoipProtocolLayer::sendUdpRequest(const QByteArray& request, const QString& host,
                                        quint16 port, int timeoutMs, QString& errorMessage) {
    Q_UNUSED(timeoutMs)
    if (!busDriver_) {
        errorMessage = "Bus driver not bound.";
        return false;
    }

    // 通过 TcpBusDriver 的 UDP 功能
    auto* tcpDriver = dynamic_cast<TcpBusDriver*>(busDriver_);
    if (tcpDriver) {
        return tcpDriver->sendUdp(request, host, port, errorMessage);
    }

    errorMessage = "Bus driver does not support UDP.";
    return false;
}

bool DoipProtocolLayer::receiveUdpResponse(QByteArray& response, QString& sourceHost,
                                            quint16& sourcePort, int timeoutMs,
                                            QString& errorMessage) {
    if (!busDriver_) {
        errorMessage = "Bus driver not bound.";
        return false;
    }

    auto* tcpDriver = dynamic_cast<TcpBusDriver*>(busDriver_);
    if (tcpDriver) {
        return tcpDriver->receiveUdp(response, sourceHost, sourcePort, timeoutMs, errorMessage);
    }

    errorMessage = "Bus driver does not support UDP.";
    return false;
}

// ============================================================================
// TCP 收发
// ============================================================================

bool DoipProtocolLayer::sendTcpFrame(const QByteArray& data, QString& errorMessage) {
    if (!busDriver_) {
        errorMessage = "Bus driver not bound.";
        return false;
    }

    eon::sdk::BusFrame frame;
    frame.data = data;
    const bool ok = busDriver_->send(frame, errorMessage);
    if (ok) lastTx_ = data;
    return ok;
}

bool DoipProtocolLayer::receiveTcpFrame(QByteArray& data, int timeoutMs,
                                         QString& errorMessage) {
    if (!busDriver_) {
        errorMessage = "Bus driver not bound.";
        return false;
    }

    while (true) {
        if (rxBuffer_.size() >= Doip::HeaderSize) {
            quint32 payloadLength = (static_cast<quint32>(static_cast<quint8>(rxBuffer_.at(4))) << 24) |
                                     (static_cast<quint32>(static_cast<quint8>(rxBuffer_.at(5))) << 16) |
                                     (static_cast<quint32>(static_cast<quint8>(rxBuffer_.at(6))) << 8) |
                                     static_cast<quint32>(static_cast<quint8>(rxBuffer_.at(7)));
            const qsizetype frameSize = Doip::HeaderSize + static_cast<qsizetype>(payloadLength);
            if (rxBuffer_.size() >= frameSize) {
                data = rxBuffer_.left(frameSize);
                rxBuffer_.remove(0, frameSize);
                lastRx_ = data;
                return true;
            }
        }

        eon::sdk::BusFrame frame;
        if (!busDriver_->receive(frame, timeoutMs, errorMessage)) return false;
        rxBuffer_.append(frame.data);
    }
}

} // namespace eon::infra

