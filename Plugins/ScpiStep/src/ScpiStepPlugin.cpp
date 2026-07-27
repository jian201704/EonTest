#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QRegularExpression>
#include <QThread>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "eon/sdk/IScpiIO.h"
#include "eon/sdk/IStepPlugin.h"
#include "eon/sdk/ResourceManager.h"
#include "eon/sdk/RetryPolicy.h"
#include "eon/sdk/TraceEvent.h"
#include "eon/infra/SerialScpiIO.h"
#include "eon/infra/TcpScpiIO.h"
#include "eon/infra/VisaScpiIO.h"
#include "eon/infra/ResponseDecoder.h"

namespace {

enum class IoType { Serial, Tcp, Visa, Unknown };

struct ScpiConnection {
    std::unique_ptr<eon::sdk::IScpiIO> io;
    std::mutex ioMutex;
    std::set<QString> workflows;
    eon::sdk::ResourceManager* resourceManager = nullptr;
    QString registeredResourceId;
};

// Connection pool shared by steps in one worker process. Each connection is
// serialized and is released when its final workflow reference is removed.
static std::map<QString, std::unique_ptr<ScpiConnection>> s_connectionPool;
static std::mutex s_poolMutex;

static QVariantList loadDecodeProfile(const QVariant& value) {
    const QString path = value.toString().trimmed();
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (document.isArray()) return document.array().toVariantList();
    if (document.isObject() && document.object().value("measurements").isArray())
        return document.object().value("measurements").toArray().toVariantList();
    return {};
}

static bool decodeScpiResponse(const QString& response, const QVariantMap& data,
                               QVariantMap& output, QString& errorMessage) {
    QVariantList specs = data.value("decodeSpecs").toList();
    if (specs.isEmpty()) specs = loadDecodeProfile(data.value("decodeProfile"));
    auto single = eon::infra::DecodeSpec::fromVariantMap(data);
    if (specs.isEmpty() && !single.hasExplicitDecode()) return true;
    const auto decoded = specs.isEmpty()
        ? eon::infra::ResponseDecoder::decode(response.toUtf8(), single)
        : eon::infra::ResponseDecoder::decodeMany(response.toUtf8(), specs);
    output["rawResponse"] = response;
    output["rawResponseHex"] = response.toUtf8().toHex(' ').toUpper();
    output["resultItems"] = decoded.toVariantList();
    output["measuredSamples"] = decoded.toVariantList();
    if (!decoded.success) {
        errorMessage = QString("SCPI response decode failed: %1").arg(decoded.errorMessage);
        return false;
    }
    if (!decoded.measurements.isEmpty()) {
        const auto& first = decoded.measurements.first();
        output["measuredValue"] = first.value;
        output["measuredUnit"] = first.unit;
        output["measurementName"] = first.name;
    }
    return true;
}

static QString connectionKey(const QVariantMap& cfg) {
    QString ct = cfg.value("connectType", "").toString().toLower();
    if (ct == "tcp" || ct == "tcpip" || ct == "lan" || cfg.contains("host"))
        return QString("tcp:%1:%2").arg(cfg.value("host").toString(), cfg.value("tcpPort", "5025").toString());
    return QString("serial:%1:%2").arg(cfg.value("port", "COM1").toString()).arg(cfg.value("baudRate", "9600").toString());
}

static ScpiConnection* getOrCreateConnection(const QVariantMap& cfg,
                                             const QString& workflowKey) {
    QString key = connectionKey(cfg);
    std::lock_guard<std::mutex> lock(s_poolMutex);
    auto it = s_connectionPool.find(key);
    if (it != s_connectionPool.end() && it->second) {
        it->second->workflows.insert(workflowKey);
        return it->second.get();
    }

    // 创建新连接
    QString ct = cfg.value("connectType", "").toString().toLower();
    IoType ioType = IoType::Serial;
    if (ct == "tcp" || ct == "tcpip" || ct == "lan" || cfg.contains("host")) ioType = IoType::Tcp;
    else if (ct == "visa" || ct == "gpib" || ct == "usb" || cfg.contains("gpibAddress") || cfg.contains("vendorId")) ioType = IoType::Visa;

    std::unique_ptr<eon::sdk::IScpiIO> io;
    switch (ioType) {
    case IoType::Tcp:  io = std::make_unique<eon::infra::TcpScpiIO>(); break;
    case IoType::Visa: io = std::make_unique<eon::infra::VisaScpiIO>(); break;
    default:           io = std::make_unique<eon::infra::SerialScpiIO>(); break;
    }

    if (!io->open(cfg)) return nullptr;
    auto connection = std::make_unique<ScpiConnection>();
    connection->io = std::move(io);
    connection->workflows.insert(workflowKey);
    auto* ptr = connection.get();
    s_connectionPool[key] = std::move(connection);
    return ptr;
}

} // namespace

class ScpiStepPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "scpistep.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override { return "scpi.command"; }

    void postWorkflow(eon::sdk::WorkflowContext& context) override {
        const QString workflowKey = QString("%1/%2").arg(
            context.workflowId, context.data.value("_cellId", "default").toString());
        std::lock_guard lock(s_poolMutex);
        for (auto it = s_connectionPool.begin(); it != s_connectionPool.end();) {
            auto& connection = it->second;
            connection->workflows.erase(workflowKey);
            if (!connection->workflows.empty()) {
                ++it;
                continue;
            }
            std::lock_guard ioLock(connection->ioMutex);
            if (connection->resourceManager)
                connection->resourceManager->unregisterResource(
                    connection->registeredResourceId.toStdString());
            if (connection->io) connection->io->close();
            it = s_connectionPool.erase(it);
        }
    }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        auto& d = context.data;
        QString stepId = d.value("_currentStepId", "unknown").toString();
        // 清除上一步遗留的测量值
        d.remove("measuredValue");
        d.remove("measuredUnit");
        d.remove("measurementName");
        d.remove("measurementStatus");
        QStringList trace;
        auto tr = [&](const QString& s) { trace.append(s); d.insert("scpi.trace", trace.join(" | ")); };
        auto er = [&](const QString& m) { errorMessage = trace.join(" | ") + " | " + m; };

        // 1. Get a workflow-scoped connection. The pooled connection keeps
        // transport lifetime separate from each individual SCPI command.
        eon::sdk::IScpiIO* io = nullptr;
        std::unique_ptr<eon::sdk::Lease> lease;
        QString resourceId = d.value("resourceId", d.value("resource", "")).toString();
        if (resourceId.isEmpty()) resourceId = connectionKey(d);
        const QString workflowKey = QString("%1/%2").arg(
            context.workflowId, d.value("_cellId", "default").toString());
        auto* connection = getOrCreateConnection(d, workflowKey);
        if (!connection || !connection->io) {
            er(QString("Cannot open connection for port=%1").arg(d.value("port", "?").toString()));
            return false;
        }

        if (context.resourceManager) {
            std::string rid = resourceId.toStdString();
            if (context.resourceManager->resourceState(rid) != "not_registered") {
                lease = context.resourceManager->acquire(rid, eon::sdk::LeaseMode::Exclusive);
                if (lease && lease->scpiIO()) {
                    io = lease->scpiIO();
                    tr(QString("RM lease [%1]").arg(resourceId));
                }
            } else {
                if (context.resourceManager->registerIoResource(rid, connection->io.get())) {
                    connection->resourceManager = context.resourceManager;
                    connection->registeredResourceId = resourceId;
                    lease = context.resourceManager->acquire(rid, eon::sdk::LeaseMode::Exclusive);
                    if (lease && lease->scpiIO()) {
                        io = lease->scpiIO();
                        tr(QString("RM register+lease [%1]").arg(resourceId));
                    }
                }
            }
        }

        if (!io) {
            io = connection->io.get();
            tr(QString("IO %1").arg(io->configInfo()));
        }
        std::unique_lock ioLock(connection->ioMutex);

        // 2. 设备清除
        io->deviceClear();

        // 3. 构建 SCPI 指令
        QString cmd = d.value("command", d.value("scpiCommand", "")).toString().trimmed();
        if (cmd.isEmpty()) {
            er("No 'command' specified in step config.");
            return false;
        }

        int timeoutMs = d.value("timeoutMs", d.value("timeout", 1000)).toInt();
        bool isQuery = cmd.endsWith('?') || d.value("query", false).toBool();

        // 4. 重试策略（默认 3 次，指数退避）
        eon::sdk::RetryPolicy retryPolicy;
        retryPolicy.maxRetries = d.value("maxRetries", 3).toInt();
        retryPolicy.backoffBaseMs = d.value("backoffBaseMs", 50).toInt();

        // 5. 执行 SCPI 操作（带重试）
        bool opSuccess = false;
        QString resp;
        int attempt = 0;

        while (attempt <= retryPolicy.maxRetries) {
            if (attempt > 0) {
                int waitMs = retryPolicy.calculateBackoffMs(attempt);
                QThread::msleep(waitMs);
                tr(QString("retry #%1 after %2ms").arg(attempt).arg(waitMs));
            }
            attempt++;

            QElapsedTimer timer;
            timer.start();

            if (isQuery) {
                resp = io->query(cmd, timeoutMs);
                qint64 elapsed = timer.elapsed();
                tr(QString(">> %1").arg(cmd));
                tr(QString("<< %1 (%2ms)").arg(resp.isEmpty() ? "<empty>" : resp).arg(elapsed));

                // 逐条 scpi.trace 事件（JSON Lines 粒度的 tx/rx 事件）
                if (d.contains("_scpiTraceFile")) {
                    // 写 tx 事件
                    eon::sdk::ScpiTraceEvent txEv;
                    txEv.stepId = stepId; txEv.resourceId = resourceId;
                    txEv.dir = "tx"; txEv.payload = cmd;
                    txEv.durationMs = 0; txEv.status = "ok";
                    // 写 rx 事件
                    eon::sdk::ScpiTraceEvent rxEv;
                    rxEv.stepId = stepId; rxEv.resourceId = resourceId;
                    rxEv.dir = "rx"; rxEv.payload = resp;
                    rxEv.durationMs = elapsed;
                    rxEv.status = resp.isEmpty() ? "timeout" : "ok";
                }

                // SCPI 错误检查
                QString scpiErr = io->readError();
                if (!scpiErr.isEmpty() && scpiErr != "0,\"No error\"") {
                    tr(QString("SCPI ERR: %1").arg(scpiErr));
                }

                // 期望值匹配
                QString expect = d.value("expect", d.value("expectedPattern", "")).toString();
                if (!expect.isEmpty()) {
                    QRegularExpression re(expect);
                    if (!re.match(resp).hasMatch()) {
                        if (attempt <= retryPolicy.maxRetries) continue;
                        er(QString("Response '%1' != expected '%2'").arg(resp, expect));
                        return false;
                    }
                    tr(QString("MATCH %1").arg(expect));
                }

                opSuccess = true;
                break;
            } else {
                tr(QString(">> %1").arg(cmd));
                bool ok = io->writeCommand(cmd, timeoutMs);
                qint64 elapsed = timer.elapsed();

                if (!ok) {
                    if (attempt <= retryPolicy.maxRetries) continue;
                    er(QString("Failed to send: %1").arg(cmd));
                    return false;
                }

                // SCPI 错误检查
                QString scpiErr = io->readError();
                if (!scpiErr.isEmpty() && scpiErr != "0,\"No error\"") {
                    tr(QString("SCPI ERR: %1").arg(scpiErr));
                }

                opSuccess = true;
                QThread::msleep(50);
                break;
            }
        }

        if (!opSuccess) {
            er("SCPI operation failed after all retries.");
            return false;
        }

        // 6. 保存结果
        if (isQuery) {
            QString saveKey = d.value("saveAs", d.value("saveTo", "scpi.response")).toString();
            d.insert(saveKey, resp);
            d.insert("measuredValue", resp);
            d.insert("measuredUnit", "");
            d.insert("measurementName", cmd);
            d.insert("measurementStatus", "scpi");
            if (!decodeScpiResponse(resp, d, d, errorMessage)) return false;
        }

        return true;
    }
};

#include "ScpiStepPlugin.moc"
