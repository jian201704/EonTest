#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QThread>
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <QVariantMap>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "eon/sdk/IStepPlugin.h"
#include "eon/sdk/IDriverPlugin.h"
#include "eon/infra/DoipProtocolLayer.h"
#include "eon/infra/ResponseDecoder.h"
#include "eon/infra/TcpBusDriver.h"

namespace {

quint16 addressValue(const QVariant& value, quint16 fallback) {
    const QString text = value.toString().trimmed();
    if (text.startsWith("0x", Qt::CaseInsensitive)) {
        bool ok = false;
        const auto result = text.mid(2).toUShort(&ok, 16);
        return ok ? result : fallback;
    }
    bool ok = false;
    const auto result = value.toUInt(&ok);
    return ok ? static_cast<quint16>(result) : fallback;
}

QByteArray hexPayload(const QVariant& value, const QByteArray& fallback) {
    const QByteArray text = value.toString().toLatin1();
    if (text.trimmed().isEmpty()) return fallback;
    const QByteArray payload = QByteArray::fromHex(text);
    return payload.isEmpty() && !text.trimmed().isEmpty() ? QByteArray() : payload;
}

QVariantList loadDecodeProfile(const QVariant& value) {
    const QString path = value.toString().trimmed();
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (document.isArray()) return document.array().toVariantList();
    if (document.isObject()) {
        const auto array = document.object().value("measurements");
        if (array.isArray()) return array.toArray().toVariantList();
    }
    return {};
}

bool matchesExpectedResponse(const QByteArray& response,
                             const QVariant& expectedResponse,
                             const QVariant& expectedPrefix,
                             QString& errorMessage) {
    const QByteArray exact = hexPayload(expectedResponse, {});
    const QByteArray prefix = hexPayload(expectedPrefix, {});

    if (!exact.isEmpty() && response != exact) {
        errorMessage = QString("UDS response mismatch: expected [%1], actual [%2].")
                           .arg(exact.toHex(' ').toUpper(), response.toHex(' ').toUpper());
        return false;
    }
    if (!prefix.isEmpty() && !response.startsWith(prefix)) {
        errorMessage = QString("UDS response prefix mismatch: expected [%1], actual [%2].")
                           .arg(prefix.toHex(' ').toUpper(), response.toHex(' ').toUpper());
        return false;
    }
    return true;
}

} // namespace

class DoipMasterPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "doipmaster.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    ~DoipMasterPlugin() override { closeAllSessions(); }

    QString id() const override { return "doip.master"; }

    void postWorkflow(eon::sdk::WorkflowContext& context) override {
        const QString key = QString("%1/%2").arg(context.workflowId,
                                                  context.data.value("_cellId", "default").toString());
        auto it = sessions_.find(key);
        if (it != sessions_.end()) {
            closeSession(*it.value());
            sessions_.erase(it);
        }
    }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        auto& d = context.data;
        const bool virt = d.value("virtualMode", false).toBool();
        const int timeoutMs = d.value("timeoutMs", d.value("timeout", 5000)).toInt();
        const QString host = d.value("host", d.value("ip", "172.16.0.8")).toString();
        const quint16 configuredPort = static_cast<quint16>(d.value("port", 13400).toUInt());
        // XLSX/旧配置可能把无效的空值或非数字值传到 port；DoIP 默认端口
        // 必须保持 13400，不能让 QVariant::toUInt() 静默变成 0。
        const quint16 port = configuredPort != 0 ? configuredPort : 13400;
        const quint16 source = addressValue(d.value("sourceAddress", "0x102D"), 0x102D);
        const quint16 target = addressValue(d.value("targetAddress", "0x1008"), 0x1008);
        const quint8 activationType = static_cast<quint8>(
            addressValue(d.value("activationType", "0x00"), 0x00));
        const QByteArray uds = hexPayload(d.value("udsPayload", d.value("data", "10 01")),
                                          QByteArray::fromHex("1001"));

        if (virt) {
            d["doip.host"] = host;
            d["doip.port"] = port;
            d["doip.routingActivation"] = "02 FD 00 05 00 00 00 07 10 2D 00 00 00 00 00";
            d["doip.udsRequest"] = uds.toHex(' ').toUpper();
            d["doip.udsResponse"] = d.value("simResponse", "50 01");
            d["doip.connected"] = true;
            d["doip.routingActive"] = true;
            d["udsResponse"] = d["doip.udsResponse"];
            d["resultText"] = "PASS (virtual)";
            return true;
        }

        if (uds.isEmpty()) {
            errorMessage = "Invalid or empty udsPayload; use hexadecimal bytes such as '10 01'.";
            return false;
        }

        const bool sessionMode = d.value("connectionMode", "session").toString()
                                     .compare("perStep", Qt::CaseInsensitive) != 0;
        const QString sessionKey = QString("%1/%2").arg(context.workflowId,
                                                         d.value("_cellId", "default").toString());
        auto session = sessionMode ? acquireSession(sessionKey) : std::make_shared<DoipSession>();
        if (!session->driver) {
            session->driver = std::make_unique<eon::infra::TcpBusDriver>();
            if (!session->driver->initialize(errorMessage)) return false;

            eon::sdk::BusConfig config;
            config.type = eon::sdk::BusType::TCP;
            config.busId = "doip-session";
            config.properties["host"] = host;
            config.properties["port"] = port;
            config.properties["enableUdp"] = false;
            // TCP keepalive 只用于探测 TCP 链路是否断开，不是 UDS TesterPresent。
            config.properties["keepAlive"] = d.value("tcpKeepAlive", true).toBool();
            if (!session->driver->open(config, errorMessage)) {
                closeSession(*session);
                return false;
            }

            session->doip = std::make_unique<eon::infra::DoipProtocolLayer>();
            if (!session->doip->initialize(errorMessage) ||
                !session->doip->bindBus(session->driver.get(), errorMessage)) {
                closeSession(*session);
                return false;
            }
            session->lastActivity.restart();
        } else if (!session->driver->isTcpConnected()) {
            closeSession(*session);
            errorMessage = "DoIP session TCP connection was lost.";
            return false;
        } else {
            const int idleTimeoutMs = d.value("idleTimeoutMs", 60000).toInt();
            if (idleTimeoutMs > 0 && session->lastActivity.isValid() &&
                session->lastActivity.elapsed() > idleTimeoutMs) {
                closeSession(*session);
                if (!sessionMode) {
                    errorMessage = "DoIP session idle timeout expired.";
                    return false;
                }
                session = acquireSession(sessionKey);
                session->driver = std::make_unique<eon::infra::TcpBusDriver>();
                if (!session->driver->initialize(errorMessage)) return false;
                eon::sdk::BusConfig config;
                config.type = eon::sdk::BusType::TCP;
                config.busId = "doip-session";
                config.properties["host"] = host;
                config.properties["port"] = port;
                config.properties["enableUdp"] = false;
                config.properties["keepAlive"] = d.value("tcpKeepAlive", true).toBool();
                if (!session->driver->open(config, errorMessage)) {
                    closeSession(*session);
                    return false;
                }
                session->doip = std::make_unique<eon::infra::DoipProtocolLayer>();
                if (!session->doip->initialize(errorMessage) ||
                    !session->doip->bindBus(session->driver.get(), errorMessage)) {
                    closeSession(*session);
                    return false;
                }
                session->lastActivity.restart();
            }
        }

        d["doip.host"] = host;
        d["doip.port"] = port;
        d["doip.connected"] = true;
        d["doip.routingActivation"] = "02 FD 00 05 00 00 00 07 10 2D 00 00 00 00 00";
        d["doip.routingTx"] = d["doip.routingActivation"];

        if (!session->routingActive) {
            if (!session->doip->activateRouting(source, activationType, timeoutMs, errorMessage)) {
                closeSession(*session);
                if (sessionMode) sessions_.remove(sessionKey);
                return false;
            }
            session->routingActive = true;
        }
        d["doip.routingActive"] = true;

        configureKeepAlive(*session, d, source, target, timeoutMs);

        QByteArray response;
        std::unique_lock<std::mutex> ioLock(session->ioMutex);
        const int minRequestIntervalMs = qMax(0, d.value("minRequestIntervalMs", 100).toInt());
        if (session->lastDiagnostic.isValid() &&
            session->lastDiagnostic.elapsed() < minRequestIntervalMs) {
            QThread::msleep(static_cast<unsigned long>(
                minRequestIntervalMs - session->lastDiagnostic.elapsed()));
        }
        // 某些 ECU 要求在连续诊断请求之间先收到 TesterPresent；周期线程
        // 可能尚未到达下一周期，因此在下一条普通 UDS 前补发一次。
        if (d.value("udsKeepAliveBeforeRequest", false).toBool() &&
            session->keepAliveEnabled && session->hasDiagnostic &&
            uds != session->keepAlivePayload) {
            QByteArray keepAliveResponse;
            QString keepAliveError;
            if (!session->doip->sendDiagnosticMessage(
                    session->keepAliveSource, session->keepAliveTarget,
                    session->keepAlivePayload, keepAliveResponse,
                    session->keepAliveTimeoutMs, keepAliveError)) {
                // TesterPresent 是辅助保活，不应阻断真正的诊断请求；
                // 某些 ECU 会对重复/过快的 3E 报文返回 DoIP NACK 0x08，
                // 但随后仍然可以正常处理 22/19/31 等业务请求。
            }
        }
        if (!session->doip->sendDiagnosticMessage(source, target, uds, response, timeoutMs, errorMessage)) {
            d["doip.tx"] = session->doip->lastTxHex();
            d["doip.rx"] = session->doip->lastRxHex();
            if (!session->doip->lastRxHex().isEmpty()) {
                errorMessage += QString(" [TX=%1 RX=%2]")
                                    .arg(session->doip->lastTxHex(), session->doip->lastRxHex());
            }
            ioLock.unlock();
            closeSession(*session);
            if (sessionMode) sessions_.remove(sessionKey);
            return false;
        }
        ioLock.unlock();
        session->hasDiagnostic = true;
        session->lastDiagnostic.restart();

        d["doip.udsRequest"] = uds.toHex(' ').toUpper();
        d["doip.udsResponse"] = response.toHex(' ').toUpper();
        d["measurementName"] = d.value("testItem", "UDS 10 01").toString();
        d["doip.tx"] = session->doip->lastTxHex();
        d["doip.rx"] = session->doip->lastRxHex();
        d["udsResponse"] = response.toHex(' ').toUpper();
        d["measuredValue"] = response.toHex(' ').toUpper();
        d["measuredUnit"] = "hex";

        // 没有解码配置时保持历史 HEX 输出；配置后统一走协议无关的解码器。
        QVariantList decodeSpecs = d.value("decodeSpecs").toList();
        if (decodeSpecs.isEmpty()) decodeSpecs = loadDecodeProfile(d.value("decodeProfile"));
        const auto decodeSpec = eon::infra::DecodeSpec::fromVariantMap(d);
        if (!decodeSpecs.isEmpty() || decodeSpec.hasExplicitDecode()) {
            const auto decoded = !decodeSpecs.isEmpty()
                ? eon::infra::ResponseDecoder::decodeMany(response, decodeSpecs)
                : eon::infra::ResponseDecoder::decode(response, decodeSpec);
            d["rawResponseHex"] = response.toHex(' ').toUpper();
            d["rawValue"] = decoded.measurements.isEmpty()
                                 ? QVariant()
                                 : decoded.measurements.first().rawValue;
            d["resultItems"] = decoded.toVariantList();
            d["measuredSamples"] = decoded.toVariantList();
            d["decodeType"] = decodeSpec.type;
            d["formula"] = decodeSpec.formula;
            if (!decoded.success) {
                d["decodeError"] = decoded.errorMessage;
                d["resultText"] = "FAIL";
                errorMessage = QString("Response decode failed: %1").arg(decoded.errorMessage);
                closeSession(*session);
                if (sessionMode) sessions_.remove(sessionKey);
                return false;
            }
            if (decoded.measurements.isEmpty()) {
                errorMessage = "Response decode produced no measurements.";
                return false;
            }
            const auto& measurement = decoded.measurements.first();
            d["measuredValue"] = measurement.value;
            d["measuredUnit"] = measurement.unit;
            d["measurementName"] = measurement.name.isEmpty()
                                        ? d.value("measurementName") : measurement.name;
            d["decodeError"] = QString();
        }

        // 可选响应校验：expectedResponse 为完整匹配，expectedPrefix 为前缀匹配。
        // 两者都配置时，先执行完整匹配，再执行前缀匹配。
        if (!matchesExpectedResponse(response,
                                     d.value("expectedResponse"),
                                     d.value("expectedPrefix"),
                                     errorMessage)) {
            d["resultText"] = "FAIL";
            closeSession(*session);
            if (sessionMode) sessions_.remove(sessionKey);
            return false;
        }
        d["resultText"] = "PASS";
        session->lastActivity.restart();

        // session 模式默认保持 TCP 和 DoIP 路由状态，直到显式结束、空闲超时或异常。
        const bool closeRequested = d.value("closeConnection", false).toBool();
        if (!sessionMode || closeRequested) {
            closeSession(*session);
            if (sessionMode) sessions_.remove(sessionKey);
        }
        return true;
    }

private:
    struct DoipSession {
        std::unique_ptr<eon::infra::TcpBusDriver> driver;
        std::unique_ptr<eon::infra::DoipProtocolLayer> doip;
        bool routingActive = false;
        QElapsedTimer lastActivity;
        std::mutex ioMutex;
        std::mutex keepAliveMutex;
        std::condition_variable keepAliveCondition;
        std::thread keepAliveThread;
        std::atomic_bool keepAliveStop{false};
        bool keepAliveEnabled = false;
        int keepAliveIntervalMs = 2000;
        QByteArray keepAlivePayload = QByteArray::fromHex("3E00");
        quint16 keepAliveSource = 0x102D;
        quint16 keepAliveTarget = 0x1008;
        int keepAliveTimeoutMs = 1000;
        bool hasDiagnostic = false;
        QElapsedTimer lastDiagnostic;
    };

    static void configureKeepAlive(DoipSession& session, const QVariantMap& data,
                                   quint16 source, quint16 target, int timeoutMs) {
        const bool enabled = data.value("udsKeepAliveEnabled", false).toBool();
        const int intervalMs = qMax(250, data.value("udsKeepAliveIntervalMs", 2000).toInt());
        const QByteArray payload = hexPayload(data.value("udsKeepAlivePayload", "3E 00"),
                                              QByteArray::fromHex("3E00"));
        {
            std::lock_guard<std::mutex> lock(session.keepAliveMutex);
            session.keepAliveEnabled = enabled;
            session.keepAliveIntervalMs = intervalMs;
            session.keepAlivePayload = payload.isEmpty() ? QByteArray::fromHex("3E00") : payload;
            session.keepAliveSource = source;
            session.keepAliveTarget = target;
            session.keepAliveTimeoutMs = qMin(timeoutMs, 1000);
        }
        session.keepAliveCondition.notify_all();
        if (enabled && !session.keepAliveThread.joinable()) {
            session.keepAliveStop.store(false);
            session.keepAliveThread = std::thread([&session] {
                std::unique_lock<std::mutex> waitLock(session.keepAliveMutex);
                while (!session.keepAliveStop.load()) {
                    const int interval = session.keepAliveIntervalMs;
                    session.keepAliveCondition.wait_for(
                        waitLock, std::chrono::milliseconds(interval),
                        [&session] { return session.keepAliveStop.load(); });
                    if (session.keepAliveStop.load() || !session.keepAliveEnabled) continue;

                    const QByteArray payload = session.keepAlivePayload;
                    const quint16 sourceAddr = session.keepAliveSource;
                    const quint16 targetAddr = session.keepAliveTarget;
                    const int timeout = session.keepAliveTimeoutMs;
                    waitLock.unlock();
                    {
                        std::lock_guard<std::mutex> ioLock(session.ioMutex);
                        if (session.doip && session.driver && session.driver->isTcpConnected()) {
                            QByteArray response;
                            QString ignoredError;
                            session.doip->sendDiagnosticMessage(
                                sourceAddr, targetAddr, payload, response, timeout, ignoredError);
                        }
                    }
                    waitLock.lock();
                }
            });
        }
    }

    std::shared_ptr<DoipSession> acquireSession(const QString& key) {
        auto it = sessions_.find(key);
        if (it != sessions_.end()) return it.value();
        auto session = std::make_shared<DoipSession>();
        sessions_.insert(key, session);
        return session;
    }

    static void closeSession(DoipSession& session) {
        session.keepAliveStop.store(true);
        session.keepAliveCondition.notify_all();
        if (session.keepAliveThread.joinable()) session.keepAliveThread.join();
        if (session.doip) session.doip->shutdown();
        if (session.driver) session.driver->shutdown();
        session.doip.reset();
        session.driver.reset();
        session.routingActive = false;
    }

    void closeAllSessions() {
        for (auto& session : sessions_) {
            if (session) closeSession(*session);
        }
        sessions_.clear();
    }

    QHash<QString, std::shared_ptr<DoipSession>> sessions_;
};

#include "DoipMasterPlugin.moc"
