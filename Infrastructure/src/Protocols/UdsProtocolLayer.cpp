#include "eon/infra/UdsProtocolLayer.h"

#include <QDateTime>

namespace eon::infra {

// ============================================================================
// ISO-TP 常量
// ============================================================================
namespace IsoTp {
    // PCI (Protocol Control Information) 类型
    constexpr quint8 SingleFrame    = 0x00; // SF: bits 7-4 = 0
    constexpr quint8 FirstFrame     = 0x10; // FF: bits 7-4 = 1
    constexpr quint8 ConsecutiveFrame = 0x20; // CF: bits 7-4 = 2
    constexpr quint8 FlowControl    = 0x30; // FC: bits 7-4 = 3

    // Flow Control 标志
    constexpr quint8 ClearToSend    = 0x00;
    constexpr quint8 Wait           = 0x01;
    constexpr quint8 Overflow       = 0x02;
}

UdsProtocolLayer::UdsProtocolLayer(QObject* parent)
    : QObject(parent) {}

UdsProtocolLayer::~UdsProtocolLayer() {
    shutdown();
}

QVariantMap UdsProtocolLayer::capabilities() const {
    return {
        {"protocol", "UDS"},
        {"standard", "ISO 14229-1:2020"},
        {"transport", "ISO 15765-2 DoCAN"},
        {"supportsCanFd", false},
        {"supportsFunctional", (functionalCanId_ != 0)},
        {"maxDataSize", 4095},
        {"services", QVariantList{
            "DiagnosticSessionControl (0x10)",
            "EcuReset (0x11)",
            "SecurityAccess (0x27)",
            "ReadDataByIdentifier (0x22)",
            "WriteDataByIdentifier (0x2E)",
            "RoutineControl (0x31)",
            "TesterPresent (0x3E)",
            "ReadDtcInformation (0x19)"
        }}
    };
}

bool UdsProtocolLayer::initialize(QString& errorMessage) {
    Q_UNUSED(errorMessage)
    if (initialized_) return true;
    initialized_ = true;
    return true;
}

void UdsProtocolLayer::shutdown() {
    unbindBus();
    initialized_ = false;
}

bool UdsProtocolLayer::bindBus(eon::sdk::IBusDriver* busDriver, QString& errorMessage) {
    if (!initialized_) {
        errorMessage = "UDS protocol layer not initialized.";
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

    // 检查总线类型
    const auto type = busDriver->busType();
    if (type != eon::sdk::BusType::CAN && type != eon::sdk::BusType::CANFD &&
        type != eon::sdk::BusType::Virtual) {
        errorMessage = "UDS protocol currently only supports CAN/CAN FD/Virtual bus.";
        return false;
    }

    busDriver_ = busDriver;
    return true;
}

void UdsProtocolLayer::unbindBus() {
    busDriver_ = nullptr;
}

bool UdsProtocolLayer::isReady() const {
    return initialized_ && busDriver_ && busDriver_->isOpen();
}

// ============================================================================
// 底层收发
// ============================================================================

bool UdsProtocolLayer::sendRequest(const QByteArray& request, QByteArray& response,
                                    int timeoutMs, QString& errorMessage) {
    if (!isReady()) {
        errorMessage = "UDS protocol layer is not ready.";
        return false;
    }

    // 编码 ISO-TP 单帧
    const QByteArray frame = encodeSingleFrame(request);

    eon::sdk::BusFrame sendFrame = buildCanFrame(requestCanId_, frame);
    eon::sdk::BusFrame recvFrame;

    if (!busDriver_->send(sendFrame, errorMessage)) {
        return false;
    }
    txCount_++;

    // 等待响应
    if (!busDriver_->receive(recvFrame, timeoutMs, errorMessage)) {
        timeoutCount_++;
        return false;
    }
    rxCount_++;

    // 解码 ISO-TP 响应
    if (!decodeSingleFrame(recvFrame.data, response)) {
        errorMessage = "Failed to decode ISO-TP response frame.";
        return false;
    }

    // 检查否定响应
    quint8 nrc = 0;
    if (isNegativeResponse(response, &nrc)) {
        nrcCount_++;
        errorMessage = QString("UDS Negative Response: NRC 0x%1")
                           .arg(nrc, 2, 16, QChar('0')).toUpper();
        return false;
    }

    return true;
}

bool UdsProtocolLayer::sendRequestEx(const eon::sdk::BusFrame& requestFrame,
                                      eon::sdk::BusFrame& responseFrame,
                                      int timeoutMs, QString& errorMessage) {
    if (!isReady()) {
        errorMessage = "UDS protocol layer is not ready.";
        return false;
    }

    if (!busDriver_->send(requestFrame, errorMessage)) {
        return false;
    }
    txCount_++;

    if (!busDriver_->receive(responseFrame, timeoutMs, errorMessage)) {
        timeoutCount_++;
        return false;
    }
    rxCount_++;
    return true;
}

// ============================================================================
// UDS 高层服务
// ============================================================================

bool UdsProtocolLayer::diagnosticSessionControl(quint8 sessionType, int timeoutMs,
                                                  QString& errorMessage) {
    QByteArray request;
    request.append(static_cast<char>(UdsService::DiagnosticSessionControl));
    request.append(static_cast<char>(sessionType));

    QByteArray response;
    if (!sendUdsRequest(request, response, timeoutMs, errorMessage)) {
        return false;
    }

    // 响应格式: 50 xx (sessionType echo)
    if (response.size() < 2 || response.at(1) != sessionType) {
        errorMessage = QString("Unexpected session control response.");
        return false;
    }
    return true;
}

bool UdsProtocolLayer::ecuReset(quint8 resetType, int timeoutMs, QString& errorMessage) {
    QByteArray request;
    request.append(static_cast<char>(UdsService::EcuReset));
    request.append(static_cast<char>(resetType));

    QByteArray response;
    return sendUdsRequest(request, response, timeoutMs, errorMessage);
}

bool UdsProtocolLayer::securityAccessRequestSeed(quint8 level, QByteArray& seed,
                                                   int timeoutMs, QString& errorMessage) {
    QByteArray request;
    request.append(static_cast<char>(UdsService::SecurityAccess));
    request.append(static_cast<char>(level)); // odd = requestSeed

    QByteArray response;
    if (!sendUdsRequest(request, response, timeoutMs, errorMessage)) {
        return false;
    }

    // 响应格式: 67 level seed...
    if (response.size() < 3) {
        errorMessage = "Security access response too short.";
        return false;
    }
    seed = response.mid(2); // skip 67 + level
    return true;
}

bool UdsProtocolLayer::securityAccessSendKey(quint8 level, const QByteArray& key,
                                               int timeoutMs, QString& errorMessage) {
    QByteArray request;
    request.append(static_cast<char>(UdsService::SecurityAccess));
    request.append(static_cast<char>(level + 1)); // even = sendKey
    request.append(key);

    QByteArray response;
    return sendUdsRequest(request, response, timeoutMs, errorMessage);
}

bool UdsProtocolLayer::readDataByIdentifier(quint16 did, QByteArray& data,
                                              int timeoutMs, QString& errorMessage) {
    QByteArray request;
    request.append(static_cast<char>(UdsService::ReadDataByIdentifier));
    request.append(static_cast<char>((did >> 8) & 0xFF));
    request.append(static_cast<char>(did & 0xFF));

    QByteArray response;
    if (!sendUdsRequest(request, response, timeoutMs, errorMessage)) {
        return false;
    }

    // 响应格式: 62 didHi didLo data...
    if (response.size() < 4) {
        errorMessage = "ReadDataByIdentifier response too short.";
        return false;
    }
    data = response.mid(3); // skip 62 + didHi + didLo
    return true;
}

bool UdsProtocolLayer::writeDataByIdentifier(quint16 did, const QByteArray& data,
                                               int timeoutMs, QString& errorMessage) {
    QByteArray request;
    request.append(static_cast<char>(UdsService::WriteDataByIdentifier));
    request.append(static_cast<char>((did >> 8) & 0xFF));
    request.append(static_cast<char>(did & 0xFF));
    request.append(data);

    QByteArray response;
    return sendUdsRequest(request, response, timeoutMs, errorMessage);
}

bool UdsProtocolLayer::testerPresent(quint8 subFunction, int timeoutMs,
                                      QString& errorMessage) {
    QByteArray request;
    request.append(static_cast<char>(UdsService::TesterPresent));
    request.append(static_cast<char>(subFunction)); // 0x00=no response, 0x80=no suppress

    QByteArray response;
    return sendUdsRequest(request, response, timeoutMs, errorMessage);
}

bool UdsProtocolLayer::readDtcByStatusMask(quint8 statusMask, QVariantList& dtcs,
                                             int timeoutMs, QString& errorMessage) {
    QByteArray request;
    request.append(static_cast<char>(UdsService::ReadDtcInformation));
    request.append(static_cast<char>(0x02)); // reportDTCByStatusMask
    request.append(static_cast<char>(statusMask));

    QByteArray response;
    if (!sendUdsRequest(request, response, timeoutMs, errorMessage)) {
        return false;
    }

    // 响应格式: 59 02 statusMask dtcRecord[] 
    // dtcRecord = dtcHigh dtcMiddle dtcLow status
    if (response.size() < 4) {
        // 可能无 DTC
        return true;
    }

    dtcs.clear();
    int offset = 3; // skip 59 02 statusMask
    while (offset + 4 <= response.size()) {
        QVariantMap dtc;
        const quint8 hi = static_cast<quint8>(response.at(offset));
        const quint8 mid = static_cast<quint8>(response.at(offset + 1));
        const quint8 lo = static_cast<quint8>(response.at(offset + 2));
        const quint8 status = static_cast<quint8>(response.at(offset + 3));

        dtc["code"] = QString("%1%2%3")
                          .arg(QString::number(hi, 16).toUpper().rightJustified(2, '0'))
                          .arg(QString::number(mid, 16).toUpper().rightJustified(2, '0'))
                          .arg(QString::number(lo, 16).toUpper().rightJustified(2, '0'));
        dtc["status"] = QString::number(status, 16).toUpper().rightJustified(2, '0');
        dtcs.append(dtc);
        offset += 4;
    }
    return true;
}

bool UdsProtocolLayer::routineControl(quint8 controlType, quint16 routineId,
                                       const QByteArray& params, QByteArray& result,
                                       int timeoutMs, QString& errorMessage) {
    QByteArray request;
    request.append(static_cast<char>(UdsService::RoutineControl));
    request.append(static_cast<char>(controlType));
    request.append(static_cast<char>((routineId >> 8) & 0xFF));
    request.append(static_cast<char>(routineId & 0xFF));
    if (!params.isEmpty()) {
        request.append(params);
    }

    QByteArray response;
    if (!sendUdsRequest(request, response, timeoutMs, errorMessage)) {
        return false;
    }

    // 响应格式: 71 controlType routineIdHi routineIdLo result...
    if (response.size() < 5) {
        errorMessage = "RoutineControl response too short.";
        return false;
    }
    result = response.mid(4);
    return true;
}

// ============================================================================
// 配置方法
// ============================================================================

void UdsProtocolLayer::setCanIds(quint32 requestId, quint32 responseId, quint32 functionalId) {
    requestCanId_ = requestId;
    responseCanId_ = responseId;
    functionalCanId_ = functionalId;
}

void UdsProtocolLayer::setExtendedAddressing(bool extended) {
    extendedCanId_ = extended;
}

// ============================================================================
// 内部方法
// ============================================================================

bool UdsProtocolLayer::sendUdsRequest(const QByteArray& request, QByteArray& response,
                                       int timeoutMs, QString& errorMessage) {
    return sendRequest(request, response, timeoutMs, errorMessage);
}

bool UdsProtocolLayer::isNegativeResponse(const QByteArray& response, quint8* nrc) const {
    // 否定响应格式: 0x7F serviceId nrc
    if (response.size() >= 3 && static_cast<quint8>(response.at(0)) == 0x7F) {
        if (nrc) {
            *nrc = static_cast<quint8>(response.at(2));
        }
        return true;
    }
    return false;
}

eon::sdk::BusFrame UdsProtocolLayer::buildCanFrame(quint32 canId, const QByteArray& data) const {
    eon::sdk::BusFrame frame;
    frame.id = canId;
    frame.data = data;
    frame.isExtended = extendedCanId_;
    frame.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    return frame;
}

QByteArray UdsProtocolLayer::encodeSingleFrame(const QByteArray& data) const {
    // ISO-TP 单帧: PCI(高4位=0, 低4位=size) + data
    const int size = qMin(data.size(), 7); // CAN 帧最多 8 字节, 1 字节 PCI
    QByteArray frame;
    frame.append(static_cast<char>(IsoTp::SingleFrame | size));
    frame.append(data.left(size));
    return frame;
}

bool UdsProtocolLayer::decodeSingleFrame(const QByteArray& frame, QByteArray& data) const {
    if (frame.isEmpty()) {
        return false;
    }

    const quint8 pci = static_cast<quint8>(frame.at(0));
    const quint8 pciType = pci & 0xF0;

    if (pciType == IsoTp::SingleFrame) {
        const int length = pci & 0x0F;
        if (length > frame.size() - 1) {
            return false;
        }
        data = frame.mid(1, length);
        return true;
    }

    // 当前仅支持单帧，多帧传输后续扩展
    // 简单处理：如果只有一帧数据，当作单帧处理
    data = frame.mid(1);
    return true;
}

} // namespace eon::infra

