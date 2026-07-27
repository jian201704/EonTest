#include "eon/runtime/WorkflowEngine.h"
#include "eon/sdk/Semver.h"
#include "eon/sdk/Dut.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfoList>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSet>
#include <QDateTime>
#include <QThread>

#include <future>

namespace {

constexpr const char* kSupportedContractVersion = "1.0";

QString pluginMetaString(const QJsonObject& metadata, const char* key) {
    return metadata.value(QString::fromLatin1(key)).toString();
}

QString pickTransitionTarget(
    const eon::domain::ActivityStep& step,
    const QString& preferredTarget,
    const QString& fallbackTarget
) {
    if (!preferredTarget.isEmpty()) {
        return preferredTarget;
    }
    if (!fallbackTarget.isEmpty()) {
        return fallbackTarget;
    }
    if (!step.onSuccessStepId.isEmpty()) {
        return step.onSuccessStepId;
    }
    return QString();
}

} // namespace

namespace eon::runtime {

WorkflowEngine::WorkflowEngine(eon::core::EventBus* eventBus)
    : eventBus_(eventBus)
    , matrixManager_(nullptr)  // 初始化时无 RM，setResourceManager 会设置
{}

WorkflowEngine::~WorkflowEngine()
{
    releaseAllResources();
}

bool WorkflowEngine::loadPlugins(const QString& pluginDirectory, QString* errorMessage) {
    return pluginManager_.loadPlugins(pluginDirectory, errorMessage);
}

eon::sdk::IStepPlugin* WorkflowEngine::findStepPluginById(const QString& pluginId) const {
    return pluginManager_.findStepPluginById(pluginId);
}

// ============================================================
// 结构化 SCPI 跟踪事件（JSON Lines，章节 5）
// ============================================================
void WorkflowEngine::writeScpiTrace(const QString& stepId, const QString& resourceId,
                                     const QString& dir, const QString& payload,
                                     qint64 durationMs, const QString& status,
                                     const QString& error,
                                     const eon::sdk::WorkflowContext* context)
{
    eon::sdk::ScpiTraceEvent event;
    event.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    event.cellId = cellId_;
    event.stepId = stepId;
    event.dutId = (context && context->dut) ? context->dut->dutId() : QString();
    event.resourceId = resourceId;
    event.dir = dir;
    event.payload = payload;
    event.durationMs = durationMs;
    event.status = status;
    event.error = error;
    event.threadId = reinterpret_cast<uintptr_t>(QThread::currentThreadId());

    // 写入 reports/cell-<id>/scpi.trace.jsonl
    if (!reportsDir_.isEmpty()) {
        QString cellDir = reportsDir_ + "/" + cellId_;
        QDir().mkpath(cellDir);
        QFile file(cellDir + "/scpi.trace.jsonl");
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QJsonDocument doc(event.toJson());
            file.write(doc.toJson(QJsonDocument::Compact) + "\n");
            file.close();
        }
    }
}

// ============================================================
// 收集 Analyzer 结果（章节 13）
// ============================================================
void WorkflowEngine::collectAnalyzerResults(const eon::sdk::WorkflowContext& context)
{
    auto result = eon::sdk::analyzerResultFromContextData(context.data);
    if (!result.status.empty()) {
        pendingAnalyzerResults_.push_back(std::move(result));
    }
}

// ============================================================
// 捕获环境快照（章节 16.5）
// ============================================================
void WorkflowEngine::captureSnapshot(const QString& workflowId)
{
    if (reportsDir_.isEmpty()) return;

    QString runId = QString("%1_%2").arg(workflowId, cellId_);
    auto snapshot = eon::runtime::captureEnvSnapshot(runId);

    QString snapPath = reportsDir_ + "/" + cellId_ + "/env_snapshot.json";
    eon::runtime::writeEnvSnapshot(snapPath, snapshot);
}

bool WorkflowEngine::preallocateResources(const eon::domain::WorkflowDefinition& workflow, QString* errorMessage)
{
    if (!resourceManager_) {
        // 没有 ResourceManager，跳过预分配（向后兼容）
        return true;
    }

    // 1. 从 workflow steps 中收集所有引用到的资源
    QSet<QString> resourceIds;
    for (const auto& step : workflow.steps) {
        for (auto it = step.initialData.constBegin(); it != step.initialData.constEnd(); ++it) {
            const QString& key = it.key();
            const QString& val = it.value().toString();
            if (key.contains("resource", Qt::CaseInsensitive) ||
                key.contains("port", Qt::CaseInsensitive) ||
                key.contains("visa", Qt::CaseInsensitive) ||
                key.contains("gpib", Qt::CaseInsensitive)) {
                if (!val.isEmpty() && !resourceIds.contains(val)) {
                    resourceIds.insert(val);
                }
            }
        }
        const QString ri = step.initialData.value("resourceId").toString();
        if (!ri.isEmpty()) resourceIds.insert(ri);
        const QString port = step.initialData.value("port").toString();
        if (!port.isEmpty()) resourceIds.insert(port);
    }

    // 2. 过滤已注册的资源
    std::vector<std::string> ridVec;
    for (const auto& rid : resourceIds) {
        std::string ridStr = rid.toStdString();
        if (resourceManager_->resourceState(ridStr) != "not_registered") {
            ridVec.push_back(std::move(ridStr));
        }
    }
    if (ridVec.empty()) return true;

    // 3. 依赖分析（参考 OpenTAP ResourceDependencyAnalyzer）
    ResourceDependencyAnalyzer analyzer;
    auto lookup = [this](const std::string& id) -> eon::sdk::IResource* {
        // ResourceManager 当前不提供按 ID 查 IResource* 的接口
        // 这里通过 registeredResources + resourceState 间接处理
        // TODO: ResourceManager 增加 resource() 查询方法
        return nullptr;
    };

    std::string depError;
    auto depNodes = analyzer.analyze(ridVec, lookup, &depError);

    if (analyzer.hasCircularDependency()) {
        QString err = QString("Resource dependency error: %1")
                          .arg(QString::fromStdString(analyzer.circularDependencyDescription()));
        if (errorMessage) *errorMessage = err;
        eventBus_->publish("resource.preallocating.failed", {
            {"error", err}
        });
        return false;
    }

    // 4. 按依赖层次分组并行打开（BFS 层级）
    // 先计算每个资源的层级
    std::unordered_map<std::string, int> level;
    for (const auto& node : depNodes) {
        if (node.strongDependencies.empty()) {
            level[node.resourceId] = 0;
        } else {
            int maxDepLevel = 0;
            for (const auto& dep : node.strongDependencies) {
                auto it = level.find(dep);
                if (it != level.end()) {
                    maxDepLevel = std::max(maxDepLevel, it->second + 1);
                }
            }
            level[node.resourceId] = maxDepLevel;
        }
    }

    // 按层级分组
    std::unordered_map<int, std::vector<std::string>> levelGroups;
    for (const auto& [rid, lvl] : level) {
        levelGroups[lvl].push_back(rid);
    }

    eventBus_->publish("resource.preallocating", {
        {"count", static_cast<int>(ridVec.size())},
        {"dependencyLevels", static_cast<int>(levelGroups.size())},
        {"resources", QStringList(resourceIds.values()).join(",")}
    });

    // 5. 逐层并行打开（同一层内部并行，层间串行）
    // 对应 OpenTAP 模式：强依赖先打开 → 然后打开依赖者
    int totalSucceeded = 0;
    int maxLevel = 0;
    for (const auto& [lvl, _] : levelGroups) {
        maxLevel = std::max(maxLevel, lvl);
    }
    for (int lvl = 0; lvl <= maxLevel; ++lvl) {
        auto it = levelGroups.find(lvl);
        if (it == levelGroups.end()) continue;

        int succeeded = resourceManager_->preallocateAll(it->second, eon::sdk::LeaseMode::Exclusive);
        totalSucceeded += succeeded;

        eventBus_->publish("resource.level.preallocated", {
            {"level", lvl},
            {"requested", static_cast<int>(it->second.size())},
            {"succeeded", succeeded}
        });

        if (succeeded < static_cast<int>(it->second.size())) {
            // 这一层有资源打开失败，停止后续层级
            if (errorMessage) {
                *errorMessage = QString("Resource preallocation failed at dependency level %1: %2/%3 succeeded")
                                    .arg(lvl).arg(succeeded).arg(it->second.size());
            }
            eventBus_->publish("resource.preallocating.failed", {
                {"level", lvl},
                {"error", *errorMessage}
            });
            return false;
        }
    }

    eventBus_->publish("resource.preallocated", {
        {"requested", static_cast<int>(ridVec.size())},
        {"succeeded", totalSucceeded}
    });

    return totalSucceeded == static_cast<int>(ridVec.size());
}

void WorkflowEngine::releaseAllResources()
{
    // 释放所有活跃租约
    activeLeases_.clear();
    if (resourceManager_) {
        // ResourceManager 析构时会自动 Close 所有资源
    }
}

void WorkflowEngine::setExternalParameter(const QString& key, const QVariant& value)
{
    externalParams_[key] = value;
}

void WorkflowEngine::writeStepResult(const eon::sdk::StepResult& result)
{
    if (reportsDir_.isEmpty()) return;
    QString cellDir = reportsDir_ + "/" + cellId_;
    QDir().mkpath(cellDir);
    QFile file(cellDir + "/step-results.jsonl");
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QJsonDocument doc(QJsonObject::fromVariantMap(result.toVariantMap()));
        file.write(doc.toJson(QJsonDocument::Compact) + "\n");
        file.close();
    }
}

// ============================================================
// CellWorker 管理（章节 6）
// ============================================================

CellWorker* WorkflowEngine::createCellWorker(const QString& cellId)
{
    if (reportsDir_.isEmpty()) {
        reportsDir_ = "reports";
    }

    auto worker = std::make_unique<CellWorker>(cellId, reportsDir_);
    if (!worker->start()) return nullptr;

    auto* ptr = worker.get();
    {
        std::lock_guard lock(cellWorkerMutex_);
        cellWorkers_.push_back(std::move(worker));
    }

    eventBus_->publish("cellworker.created", {
        {"cellId", cellId},
        {"reportsDir", ptr->reportsDir()}
    });

    return ptr;
}

std::vector<CellWorker*> WorkflowEngine::cellWorkers() const
{
    std::lock_guard lock(cellWorkerMutex_);
    std::vector<CellWorker*> result;
    result.reserve(cellWorkers_.size());
    for (const auto& w : cellWorkers_) {
        result.push_back(w.get());
    }
    return result;
}

void WorkflowEngine::destroyAllCellWorkers()
{
    std::lock_guard lock(cellWorkerMutex_);
    for (auto& w : cellWorkers_) {
        w->stop();
    }
    cellWorkers_.clear();
}

QStringList WorkflowEngine::unhealthyCellWorkers() const
{
    QStringList unhealthy;
    std::lock_guard lock(cellWorkerMutex_);
    for (const auto& w : cellWorkers_) {
        if (!w->isHealthy()) {
            unhealthy.append(w->cellId());
        }
    }
    return unhealthy;
}

void WorkflowEngine::updateFinalVerdict(eon::sdk::Verdict stepVerdict)
{
    finalVerdict_ = eon::sdk::mergeVerdicts(finalVerdict_, stepVerdict);
}

bool WorkflowEngine::executeWorkflow(const eon::domain::WorkflowDefinition& workflow, QString* errorMessage) {
    return executeWorkflowWithParams(workflow, {}, errorMessage);
}

bool WorkflowEngine::executeWorkflowWithParams(const eon::domain::WorkflowDefinition& workflow,
                                                const QVariantMap& recipeParams,
                                                QString* errorMessage) {
    if (eventBus_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "EventBus is null.";
        }
        return false;
    }

    if (workflow.steps.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Workflow '%1' has no steps.").arg(workflow.workflowId);
        }
        return false;
    }

    QHash<QString, const eon::domain::ActivityStep*> stepById;
    for (const auto& step : workflow.steps) {
        if (step.stepId.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Workflow '%1' contains a step with empty stepId.").arg(workflow.workflowId);
            }
            return false;
        }
        if (stepById.contains(step.stepId)) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Workflow '%1' contains duplicate stepId '%2'.").arg(workflow.workflowId, step.stepId);
            }
            return false;
        }
        stepById.insert(step.stepId, &step);
    }

    QString currentStepId = workflow.entryStepId;
    if (currentStepId.isEmpty()) {
        currentStepId = workflow.steps.first().stepId;
    }

    if (!stepById.contains(currentStepId)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Workflow '%1' entry step '%2' does not exist.")
                                .arg(workflow.workflowId, currentStepId);
        }
        return false;
    }

    eon::sdk::WorkflowContext context;
    context.workflowId = workflow.workflowId;
    context.data = workflow.initialData;
    // 供需要跨步骤复用连接的协议插件隔离不同 CELL 的会话。
    context.data.insert("_cellId", cellId_);
    context.resourceManager = resourceManager_; // 注入资源管理器
    // 合并配方参数（配方值覆盖 initialData 中的同名 key）
    for (auto it = recipeParams.constBegin(); it != recipeParams.constEnd(); ++it) {
        context.data.insert(it.key(), it.value());
    }

    // 将 ResourceManager 注入 MatrixManager
    // (MatrixManager 在构造时无 RM，由 WorkflowEngine 在首次执行时设置)

    // 注入 DUT 插件（从 WorkflowDefinition.dutPluginId 查找）
    if (!workflow.dutPluginId.isEmpty()) {
        auto* dut = pluginManager_.findDutPluginById(workflow.dutPluginId);
        if (dut) {
            // 应用 DUT 配置（通过 Dut 基类的 setter）
            if (auto* dutBase = dynamic_cast<eon::sdk::Dut*>(dut)) {
                if (workflow.dutConfig.contains("serialNumber"))
                    dutBase->setDutId(workflow.dutConfig.value("serialNumber").toString());
                if (workflow.dutConfig.contains("modelName"))
                    dutBase->setModelName(workflow.dutConfig.value("modelName").toString());
                if (workflow.dutConfig.contains("firmwareVersion"))
                    dutBase->setFirmwareVersion(workflow.dutConfig.value("firmwareVersion").toString());
                if (workflow.dutConfig.contains("description"))
                    dutBase->setDescription(workflow.dutConfig.value("description").toString());
            }
            context.dut = dut;
            // 注册 DUT 资源到 ResourceManager 并打开连接
            if (resourceManager_) {
                auto* res = dynamic_cast<eon::sdk::IResource*>(dut);
                if (res) {
                    resourceManager_->registerResource(workflow.dutPluginId.toStdString(), res);
                    res->open();
                }
            }
        }
    }

    // 合并外部参数（对标 OpenTAP ExternalParameters，CLI --external 传入）
    for (auto it = externalParams_.constBegin(); it != externalParams_.constEnd(); ++it) {
        context.data.insert(it.key(), it.value());
    }

    eventBus_->publish("workflow.started", {
        {"workflowId", context.workflowId}
    });

    // 捕获环境快照（章节 16.5）
    captureSnapshot(context.workflowId);

    // PrePlanRun：预分配工作流所需资源
    if (!preallocateResources(workflow, errorMessage)) {
        eventBus_->publish("workflow.failed", {
            {"workflowId", context.workflowId},
            {"error", errorMessage ? *errorMessage : "resource preallocation failed"}
        });
        return false;
    }

    // 重置最终 Verdict
    finalVerdict_ = eon::sdk::Verdict::NotSet;

    QStringList executedStepIds;
    auto runCompensation = [&]() {
        QSet<QString> executedCompensationStepIds;
        for (auto it = executedStepIds.crbegin(); it != executedStepIds.crend(); ++it) {
            const auto* executedStep = stepById.value(*it, nullptr);
            if (executedStep == nullptr || executedStep->compensationStepId.isEmpty()) {
                continue;
            }
            if (executedCompensationStepIds.contains(executedStep->compensationStepId)) {
                continue;
            }

            const auto* compensationStep = stepById.value(executedStep->compensationStepId, nullptr);
            if (compensationStep == nullptr) {
                eventBus_->publish("compensation.failed", {
                    {"workflowId", context.workflowId},
                    {"stepId", executedStep->stepId},
                    {"compensationStepId", executedStep->compensationStepId},
                    {"error", "Compensation step does not exist."}
                });
                executedCompensationStepIds.insert(executedStep->compensationStepId);
                continue;
            }

            auto* compensationPlugin = findStepPluginById(compensationStep->pluginId);
            if (compensationPlugin == nullptr) {
                eventBus_->publish("compensation.failed", {
                    {"workflowId", context.workflowId},
                    {"stepId", executedStep->stepId},
                    {"compensationStepId", compensationStep->stepId},
                    {"pluginId", compensationStep->pluginId},
                    {"error", "Compensation plugin is not loaded."}
                });
                executedCompensationStepIds.insert(compensationStep->stepId);
                continue;
            }

            eventBus_->publish("compensation.started", {
                {"workflowId", context.workflowId},
                {"stepId", executedStep->stepId},
                {"compensationStepId", compensationStep->stepId},
                {"pluginId", compensationStep->pluginId}
            });

            QString compensationError;
            context.data.insert("_currentStepId", compensationStep->stepId);
            context.data.insert("_executionMode", "compensation");
            if (!compensationPlugin->executeStep(context, compensationError)) {
                eventBus_->publish("compensation.failed", {
                    {"workflowId", context.workflowId},
                    {"stepId", executedStep->stepId},
                    {"compensationStepId", compensationStep->stepId},
                    {"pluginId", compensationStep->pluginId},
                    {"error", compensationError}
                });
            } else {
                eventBus_->publish("compensation.finished", {
                    {"workflowId", context.workflowId},
                    {"stepId", executedStep->stepId},
                    {"compensationStepId", compensationStep->stepId},
                    {"pluginId", compensationStep->pluginId}
                });
            }

            executedCompensationStepIds.insert(compensationStep->stepId);
        }
    };

    auto failWorkflow = [&](const QVariantMap& payload, const QString& message) {
        QVariantMap failurePayload = payload;
        failurePayload.insert("workflowId", context.workflowId);
        if (!message.isEmpty()) {
            failurePayload.insert("error", message);
        }
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        eventBus_->publish("workflow.failed", failurePayload);
        runCompensation();
        // Failure path must release engine-owned leases before notifying
        // plugins, matching the normal PostPlanRun cleanup contract.
        releaseAllResources();
        for (auto it = pluginManager_.stepPlugins().cbegin();
             it != pluginManager_.stepPlugins().cend(); ++it) {
            if (it.value() != nullptr) it.value()->postWorkflow(context);
        }
        return false;
    };

    enum class StepRunOutcome {
        Success,
        Skipped,
        ContinueOnError,
        HardFailed
    };

    auto runStep = [&](const eon::domain::ActivityStep* step, QString* failureMessage) -> StepRunOutcome {
        if (step == nullptr) {
            if (failureMessage != nullptr) {
                *failureMessage = "Step is null.";
            }
            return StepRunOutcome::HardFailed;
        }

        if (!step->conditionKey.isEmpty()) {
            const QString actualValue = context.data.value(step->conditionKey).toString();
            if (actualValue != step->conditionEquals) {
                eventBus_->publish("activity.skipped", {
                    {"stepId", step->stepId},
                    {"pluginId", step->pluginId},
                    {"conditionKey", step->conditionKey},
                    {"expected", step->conditionEquals},
                    {"actual", actualValue}
                });
                return StepRunOutcome::Skipped;
            }
        }

        if (step->pluginId.isEmpty()) {
            if (failureMessage != nullptr) {
                *failureMessage = QString("Workflow step '%1' has empty pluginId.").arg(step->stepId);
            }
            return StepRunOutcome::HardFailed;
        }

        auto* plugin = findStepPluginById(step->pluginId);
        if (plugin == nullptr) {
            if (failureMessage != nullptr) {
                *failureMessage = QString("Plugin '%1' is not loaded.").arg(step->pluginId);
            }
            return StepRunOutcome::HardFailed;
        }

        const int maxAttempts = step->policy.maxRetries + 1;
        QString stepFailureMessage;
        // 清除上一步遗留的 SCPI trace
        context.data.remove("powerScpiTrace");
        context.data.remove("dmmScpiTrace");
        context.data.remove("power.scpiTrace");
        context.data.remove("dmm.scpiTrace");
        context.data.remove("scpi.trace");
        // 合并步骤级参数（覆盖全局 initialData）
        for (auto it = step->initialData.cbegin(); it != step->initialData.cend(); ++it) {
            context.data.insert(it.key(), it.value());
        }
        for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
            eventBus_->publish("activity.started", {
                {"stepId", step->stepId},
                {"pluginId", step->pluginId},
                {"attempt", attempt},
                {"timeoutMs", step->policy.timeoutMs}
            });

            QString pluginError;
            QElapsedTimer timer;
            timer.start();
            context.data.insert("_currentStepId", step->stepId);
            context.data.insert("_executionMode", "normal");

            // 步骤执行前回调（对标 OpenTAP PrePlanRun）
            plugin->preExecute(context);

            const bool pluginSucceeded = plugin->executeStep(context, pluginError);
            const qint64 elapsedMs = timer.elapsed();
            const bool timedOut = step->policy.timeoutMs > 0 && elapsedMs > step->policy.timeoutMs;

            // 步骤执行后回调（对标 OpenTAP PostPlanRun，无论成败）
            plugin->postExecute(context);

            if (pluginSucceeded && !timedOut) {
                QVariantMap activityPayload{
                    {"workflowId", context.workflowId},
                    {"stepId", step->stepId},
                    {"pluginId", step->pluginId},
                    {"attempt", attempt},
                    {"elapsedMs", elapsedMs}
                };
                if (context.data.contains("measuredValue")) {
                    activityPayload.insert("measuredValue", context.data.value("measuredValue"));
                }
                if (context.data.contains("measuredUnit")) {
                    activityPayload.insert("measuredUnit", context.data.value("measuredUnit"));
                }
                if (context.data.contains("measuredSamples")) {
                    activityPayload.insert("measuredSamples", context.data.value("measuredSamples"));
                }
                if (context.data.contains("resultItems")) {
                    activityPayload.insert("resultItems", context.data.value("resultItems"));
                }
                if (context.data.contains("measurementName")) {
                    activityPayload.insert("measurementName", context.data.value("measurementName"));
                }
                // DoIP/二进制协议调试信息：保留最近一次完整 TX/RX 帧，供 Studio 日志显示。
                if (context.data.contains("doip.tx"))
                    activityPayload.insert("doipTx", context.data.value("doip.tx"));
                if (context.data.contains("doip.routingTx"))
                    activityPayload.insert("doipRoutingTx", context.data.value("doip.routingTx"));
                if (context.data.contains("doip.rx"))
                    activityPayload.insert("doipRx", context.data.value("doip.rx"));
                // 透传限值和判定结果
                if (context.data.contains("lowerLimit"))
                    activityPayload.insert("lowerLimit", context.data.value("lowerLimit"));
                if (context.data.contains("upperLimit"))
                    activityPayload.insert("upperLimit", context.data.value("upperLimit"));
                if (context.data.contains("resultText"))
                    activityPayload.insert("resultText", context.data.value("resultText"));

                // 结构化 scpi.trace（JSON Lines，章节 5）
                QString scpiTraceStr;
                for (const auto& key : {"scpi.trace", "powerScpiTrace", "dmmScpiTrace",
                                         "power.scpiTrace", "dmm.scpiTrace"}) {
                    if (context.data.contains(key)) {
                        scpiTraceStr = context.data.value(key).toString();
                        break;
                    }
                }
                if (!scpiTraceStr.isEmpty()) {
                    activityPayload.insert("scpiTrace", scpiTraceStr);
                    writeScpiTrace(step->stepId, step->pluginId, "tx",
                                    scpiTraceStr, elapsedMs, "ok", {}, &context);
                }

                // 主动轮询 AnalyzerPlugin（对每一步执行所有 analyzer）
                QVariantMap analyzerMergeResult;
                const auto& analyzerMap = pluginManager_.analyzers();
                for (auto aIt = analyzerMap.cbegin(); aIt != analyzerMap.cend(); ++aIt) {
                    auto* analyzerPlugin = aIt.value();
                    if (!analyzerPlugin) continue;
                    QVariantMap result;
                    QString err;
                    if (!analyzerPlugin->analyze(context, result, err)) continue;
                    for (auto rIt = result.constBegin(); rIt != result.constEnd(); ++rIt)
                        context.data.insert(rIt.key(), rIt.value());
                    for (auto rIt = result.constBegin(); rIt != result.constEnd(); ++rIt)
                        analyzerMergeResult.insert(rIt.key(), rIt.value());
                }

                // 合并 Analyzer 结果到 Verdict
                collectAnalyzerResults(context);
                auto mergedVerdict = eon::sdk::mergeAnalyzerResults(pendingAnalyzerResults_);
                updateFinalVerdict(mergedVerdict);

                // 转发 analyzer 判定结果
                if (analyzerMergeResult.contains("analyze.passed"))
                    activityPayload.insert("analyzePassed", analyzerMergeResult.value("analyze.passed"));
                if (analyzerMergeResult.contains("analyze.value"))
                    activityPayload.insert("analyzeValue", analyzerMergeResult.value("analyze.value"));
                if (analyzerMergeResult.contains("analyze.min"))
                    activityPayload.insert("analyzeMin", analyzerMergeResult.value("analyze.min"));
                if (analyzerMergeResult.contains("analyze.max"))
                    activityPayload.insert("analyzeMax", analyzerMergeResult.value("analyze.max"));
                if (analyzerMergeResult.contains("analyze.unit"))
                    activityPayload.insert("analyzeUnit", analyzerMergeResult.value("analyze.unit"));
                if (analyzerMergeResult.contains("analyze.message"))
                    activityPayload.insert("analyzeMessage", analyzerMergeResult.value("analyze.message"));
                eventBus_->publish("activity.finished", activityPayload);
                executedStepIds.append(step->stepId);

                // 写 step-results.jsonl
                if (!reportsDir_.isEmpty()) {
                    QString cellDir = reportsDir_ + "/" + cellId_;
                    QDir().mkpath(cellDir);
                    QFile stepFile(cellDir + "/step-results.jsonl");
                    if (stepFile.open(QIODevice::Append | QIODevice::Text)) {
                        QJsonObject stepResult;
                        stepResult["ts"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                        stepResult["stepId"] = step->stepId;
                        stepResult["pluginId"] = step->pluginId;
                        stepResult["workflowId"] = context.workflowId;
                        stepResult["cellId"] = cellId_;
                        stepResult["attempt"] = attempt;
                        stepResult["elapsedMs"] = elapsedMs;
                        stepResult["verdict"] = eon::sdk::verdictToString(mergedVerdict);
                        stepResult["dutId"] = context.dut ? context.dut->dutId() : QString();
                        // 结构化 StepResult 表格（对标 OpenTAP ResultTable）
                        if (context.data.contains("_stepResult")) {
                            stepResult["resultTable"] = context.data.value("_stepResult").toJsonValue();
                        }
                        if (context.data.contains("measuredValue"))
                            stepResult["measuredValue"] = context.data.value("measuredValue").toString();
                        if (context.data.contains("measuredUnit"))
                            stepResult["measuredUnit"] = context.data.value("measuredUnit").toString();
                        if (context.data.contains("analyze.message"))
                            stepResult["analyzeMessage"] = context.data.value("analyze.message").toString();
                        QJsonDocument doc(stepResult);
                        stepFile.write(doc.toJson(QJsonDocument::Compact) + "\n");
                        stepFile.close();
                    }
                }

                return StepRunOutcome::Success;
            }

            if (timedOut) {
                stepFailureMessage = QString("Step '%1' timed out: %2ms > %3ms.")
                                         .arg(step->stepId)
                                         .arg(elapsedMs)
                                         .arg(step->policy.timeoutMs);
            } else if (!pluginError.isEmpty()) {
                stepFailureMessage = QString("Plugin '%1' failed: %2").arg(step->pluginId, pluginError);
            } else {
                stepFailureMessage = QString("Plugin '%1' failed without details.").arg(step->pluginId);
            }

            eventBus_->publish("activity.retry", {
                {"stepId", step->stepId},
                {"pluginId", step->pluginId},
                {"attempt", attempt},
                {"maxAttempts", maxAttempts},
                {"error", stepFailureMessage}
            });
        }

        if (step->policy.failurePolicy == eon::domain::FailurePolicy::ContinueOnError) {
            executedStepIds.append(step->stepId);
            eventBus_->publish("activity.failed", {
                {"stepId", step->stepId},
                {"pluginId", step->pluginId},
                {"error", stepFailureMessage},
                {"policy", "continue_on_error"}
            });
            return StepRunOutcome::ContinueOnError;
        }

        if (failureMessage != nullptr) {
            *failureMessage = stepFailureMessage;
        }
        return StepRunOutcome::HardFailed;
    };

    QHash<QString, int> stepVisitCount;
    QSet<QString> processedParallelGroups;
    int transitionCount = 0;
    constexpr int kMaxTransitions = 1024;
    while (!currentStepId.isEmpty()) {
        transitionCount += 1;
        if (transitionCount > kMaxTransitions) {
            return failWorkflow(
                {{"stepId", currentStepId}},
                QString("Workflow '%1' exceeded max transitions (%2).")
                    .arg(workflow.workflowId)
                    .arg(kMaxTransitions)
            );
        }

        stepVisitCount[currentStepId] = stepVisitCount.value(currentStepId, 0) + 1;
        const auto* step = stepById.value(currentStepId, nullptr);
        if (step == nullptr) {
            return failWorkflow(
                {{"stepId", currentStepId}},
                QString("Workflow '%1' references missing step '%2'.")
                    .arg(workflow.workflowId, currentStepId)
            );
        }

        if (stepVisitCount.value(currentStepId) > 100) {
            return failWorkflow(
                {{"stepId", currentStepId}},
                QString("Workflow '%1' entered loop at step '%2'.")
                    .arg(workflow.workflowId, currentStepId)
            );
        }

        if (!step->parallelGroupId.isEmpty()) {
            if (!processedParallelGroups.contains(step->parallelGroupId)) {
                processedParallelGroups.insert(step->parallelGroupId);
                QList<const eon::domain::ActivityStep*> groupSteps;
                for (const auto& candidate : workflow.steps) {
                    if (candidate.parallelGroupId == step->parallelGroupId) {
                        groupSteps.append(&candidate);
                    }
                }

                eventBus_->publish("parallel.batch.started", {
                    {"workflowId", context.workflowId},
                    {"parallelGroupId", step->parallelGroupId},
                    {"stepCount", groupSteps.size()}
                });

                // --- 并行组 BreakOffer ---
                if (onBreakOffered) {
                    ExecutionControl ctrl = onBreakOffered(
                        QString("parallel:%1").arg(step->parallelGroupId));
                    if (ctrl == ExecutionControl::Abort) {
                        return failWorkflow(
                            {{"parallelGroupId", step->parallelGroupId}},
                            "Parallel batch aborted by user."
                        );
                    }
                    if (ctrl == ExecutionControl::Skip) {
                        eventBus_->publish("parallel.batch.skipped", {
                            {"parallelGroupId", step->parallelGroupId},
                            {"reason", "User skipped via BreakOffer"}
                        });
                        currentStepId = pickTransitionTarget(*step, step->onSkippedStepId, step->onSuccessStepId);
                        if (currentStepId.isEmpty()) currentStepId.clear();
                        continue;
                    }
                    if (ctrl == ExecutionControl::Pause) {
                        eventBus_->publish("workflow.paused",
                            {{"parallelGroupId", step->parallelGroupId}});
                        constexpr int kPollIntervalMs = 100;
                        constexpr int kMaxWaitMs = 300000;
                        int waitedMs = 0;
                        while (!resumeSignal_.load() && !skipSignal_.load() && !abortSignal_.load() && waitedMs < kMaxWaitMs) {
                            QThread::msleep(kPollIntervalMs);
                            waitedMs += kPollIntervalMs;
                        }
                        resumeSignal_.store(false);
                        if (abortSignal_.load()) {
                            abortSignal_.store(false);
                            return failWorkflow(
                                {{"parallelGroupId", step->parallelGroupId}},
                                "Parallel batch aborted during pause."
                            );
                        }
                        if (skipSignal_.load()) {
                            skipSignal_.store(false);
                            currentStepId = pickTransitionTarget(*step, step->onSkippedStepId, step->onSuccessStepId);
                            if (currentStepId.isEmpty()) currentStepId.clear();
                            continue;
                        }
                        eventBus_->publish("workflow.resumed",
                            {{"parallelGroupId", step->parallelGroupId}});
                    }
                }

                bool hardFailedInBatch = false;
                QString batchFailureMessage;
                QMutex batchMutex;

                // 真正并行执行组内步骤（std::async 线程池）
                {
                    std::vector<std::future<void>> futures;
                    for (const auto* batchStep : groupSteps) {
                        futures.push_back(std::async(std::launch::async, [&, batchStep]() {
                            QString stepFailureMessage;
                            StepRunOutcome outcome = runStep(batchStep, &stepFailureMessage);
                            if (outcome == StepRunOutcome::HardFailed) {
                                QMutexLocker lock(&batchMutex);
                                hardFailedInBatch = true;
                                batchFailureMessage = stepFailureMessage;
                            }
                        }));
                    }
                    // 等待所有并行步骤完成
                    for (auto& f : futures)
                        f.wait();
                }

                eventBus_->publish("parallel.batch.finished", {
                    {"workflowId", context.workflowId},
                    {"parallelGroupId", step->parallelGroupId},
                    {"status", hardFailedInBatch ? "failed" : "ok"}
                });

                if (hardFailedInBatch) {
                    if (!step->onFailureStepId.isEmpty()) {
                        if (!stepById.contains(step->onFailureStepId)) {
                            return failWorkflow(
                                {
                                    {"stepId", step->stepId},
                                    {"parallelGroupId", step->parallelGroupId}
                                },
                                QString("Parallel group '%1' failure target '%2' does not exist.")
                                    .arg(step->parallelGroupId, step->onFailureStepId)
                            );
                        }
                        currentStepId = step->onFailureStepId;
                        continue;
                    }

                    return failWorkflow(
                        {
                            {"stepId", step->stepId},
                            {"parallelGroupId", step->parallelGroupId}
                        },
                        batchFailureMessage
                    );
                }
            }

            if (!step->onSuccessStepId.isEmpty()) {
                if (!stepById.contains(step->onSuccessStepId)) {
                    return failWorkflow(
                        {
                            {"stepId", step->stepId},
                            {"parallelGroupId", step->parallelGroupId}
                        },
                        QString("Parallel group '%1' success target '%2' does not exist.")
                            .arg(step->parallelGroupId, step->onSuccessStepId)
                    );
                }
                currentStepId = step->onSuccessStepId;
                continue;
            }
            currentStepId.clear();
            continue;
        }

        // --- 步骤执行前 BreakOffer（暂停/跳过检查） ---
        if (onBreakOffered) {
            ExecutionControl ctrl = onBreakOffered(step->stepId);
            if (ctrl == ExecutionControl::Abort) {
                return failWorkflow(
                    {{"stepId", step->stepId}},
                    "Workflow aborted by user."
                );
            }
            if (ctrl == ExecutionControl::Skip) {
                eventBus_->publish("activity.skipped", {
                    {"stepId", step->stepId},
                    {"pluginId", step->pluginId},
                    {"reason", "User skipped via BreakOffer"}
                });
                updateFinalVerdict(eon::sdk::Verdict::Inconclusive);
                currentStepId = pickTransitionTarget(*step, step->onSkippedStepId, step->onSuccessStepId);
                if (currentStepId.isEmpty())
                    currentStepId.clear();
                continue;
            }
            if (ctrl == ExecutionControl::Pause) {
                // 阻塞等待外部调用 resumeExecution() 或超时
                eventBus_->publish("workflow.paused", {{"stepId", step->stepId}});
                constexpr int kPollIntervalMs = 100;
                constexpr int kMaxWaitMs = 300000; // 5min 超时
                int waitedMs = 0;
                while (!resumeSignal_.load() && !skipSignal_.load() && !abortSignal_.load() && waitedMs < kMaxWaitMs) {
                    QThread::msleep(kPollIntervalMs);
                    waitedMs += kPollIntervalMs;
                }
                resumeSignal_.store(false);
                if (abortSignal_.load()) {
                    abortSignal_.store(false);
                    return failWorkflow(
                        {{"stepId", step->stepId}},
                        "Workflow aborted during pause."
                    );
                }
                if (skipSignal_.load()) {
                    skipSignal_.store(false);
                    eventBus_->publish("activity.skipped", {
                        {"stepId", step->stepId},
                        {"pluginId", step->pluginId},
                        {"reason", "User skipped after pause"}
                    });
                    updateFinalVerdict(eon::sdk::Verdict::Inconclusive);
                    currentStepId = pickTransitionTarget(*step, step->onSkippedStepId, step->onSuccessStepId);
                    if (currentStepId.isEmpty())
                        currentStepId.clear();
                    continue;
                }
                eventBus_->publish("workflow.resumed", {{"stepId", step->stepId}});
            }
        }

        QString stepFailureMessage;
        const StepRunOutcome outcome = runStep(step, &stepFailureMessage);

        // --- 步骤级 BreakCondition 检查（对标 OpenTAP BreakCondition） ---
        auto shouldBreakOnOutcome = [&](StepRunOutcome o, const eon::domain::ActivityStep* s) -> bool {
            int mask = s->breakCondition;
            if (mask == 0) return false; // Inherit = 不中断
            if ((o == StepRunOutcome::HardFailed) && (mask & eon::domain::breakConditionMask(eon::domain::BreakCondition::BreakOnError)))
                return true;
            // ContinueOnError/HardFailed 都视为 Fail 级
            if ((o == StepRunOutcome::ContinueOnError || o == StepRunOutcome::HardFailed) &&
                (mask & eon::domain::breakConditionMask(eon::domain::BreakCondition::BreakOnFail)))
                return true;
            return false;
        };

        if (shouldBreakOnOutcome(outcome, step)) {
            eventBus_->publish("workflow.breakCondition", {
                {"stepId", step->stepId},
                {"outcome", static_cast<int>(outcome)},
                {"breakCondition", step->breakCondition}
            });
            currentStepId.clear();
            continue;
        }

        if (outcome == StepRunOutcome::HardFailed) {
            if (!step->onFailureStepId.isEmpty()) {
                if (!stepById.contains(step->onFailureStepId)) {
                    return failWorkflow(
                        {
                            {"stepId", step->stepId},
                            {"pluginId", step->pluginId}
                        },
                        QString("Step '%1' onFailure target '%2' does not exist.")
                            .arg(step->stepId, step->onFailureStepId)
                    );
                }
                currentStepId = step->onFailureStepId;
                continue;
            }
            return failWorkflow(
                {
                    {"stepId", step->stepId},
                    {"pluginId", step->pluginId}
                },
                stepFailureMessage
            );
        }

        if (outcome == StepRunOutcome::Skipped) {
            const QString nextStepId = pickTransitionTarget(*step, step->onSkippedStepId, step->onSuccessStepId);
            if (!nextStepId.isEmpty()) {
                if (!stepById.contains(nextStepId)) {
                    return failWorkflow(
                        {
                            {"stepId", step->stepId},
                            {"pluginId", step->pluginId}
                        },
                        QString("Step '%1' skip target '%2' does not exist.")
                            .arg(step->stepId, nextStepId)
                    );
                }
                currentStepId = nextStepId;
                continue;
            }
            currentStepId.clear();
            continue;
        }

        if (!step->onSuccessStepId.isEmpty()) {
            if (!stepById.contains(step->onSuccessStepId)) {
                return failWorkflow(
                    {
                        {"stepId", step->stepId},
                        {"pluginId", step->pluginId}
                    },
                    QString("Step '%1' onSuccess target '%2' does not exist.")
                        .arg(step->stepId, step->onSuccessStepId)
                );
            }
            currentStepId = step->onSuccessStepId;
            continue;
        }
        currentStepId.clear();
    }

    // PostPlanRun：释放所有资源
    releaseAllResources();
    for (auto it = pluginManager_.stepPlugins().cbegin();
         it != pluginManager_.stepPlugins().cend(); ++it) {
        if (it.value() != nullptr) it.value()->postWorkflow(context);
    }

    // 发布最终 Verdict
    eventBus_->publish("workflow.verdict", {
        {"workflowId", context.workflowId},
        {"verdict", eon::sdk::verdictToString(finalVerdict_)}
    });

    for (auto it = pluginManager_.analyzers().cbegin(); it != pluginManager_.analyzers().cend(); ++it) {
        const QString analyzerId = it.key();
        auto* analyzerPlugin = it.value();
        if (analyzerPlugin == nullptr) {
            continue;
        }

        eventBus_->publish("analyzer.started", {
            {"workflowId", context.workflowId},
            {"analyzerId", analyzerId}
        });

        QVariantMap analyzeResult;
        QString analyzeError;
        if (!analyzerPlugin->analyze(context, analyzeResult, analyzeError)) {
            QVariantMap analyzerFailedPayload{
                {"workflowId", context.workflowId},
                {"analyzerId", analyzerId},
                {"error", analyzeError}
            };
            if (analyzeResult.contains("analyze.passed")) {
                analyzerFailedPayload.insert("analyzePassed", analyzeResult.value("analyze.passed"));
            }
            if (analyzeResult.contains("analyze.value")) {
                analyzerFailedPayload.insert("analyzeValue", analyzeResult.value("analyze.value"));
            }
            if (analyzeResult.contains("analyze.min")) {
                analyzerFailedPayload.insert("analyzeMin", analyzeResult.value("analyze.min"));
            }
            if (analyzeResult.contains("analyze.max")) {
                analyzerFailedPayload.insert("analyzeMax", analyzeResult.value("analyze.max"));
            }
            if (analyzeResult.contains("analyze.unit")) {
                analyzerFailedPayload.insert("analyzeUnit", analyzeResult.value("analyze.unit"));
            }
            if (analyzeResult.contains("analyze.message")) {
                analyzerFailedPayload.insert("analyzeMessage", analyzeResult.value("analyze.message"));
            }
            eventBus_->publish("analyzer.failed", analyzerFailedPayload);
            return failWorkflow(
                {
                    {"analyzerId", analyzerId}
                },
                QString("Analyzer '%1' failed: %2").arg(analyzerId, analyzeError)
            );
        }

        for (auto resultIt = analyzeResult.constBegin(); resultIt != analyzeResult.constEnd(); ++resultIt) {
            context.data.insert(resultIt.key(), resultIt.value());
        }

        QVariantMap analyzerFinishedPayload{
            {"workflowId", context.workflowId},
            {"analyzerId", analyzerId},
            {"resultSize", analyzeResult.size()}
        };
        if (analyzeResult.contains("analyze.passed")) {
            analyzerFinishedPayload.insert("analyzePassed", analyzeResult.value("analyze.passed"));
        }
        if (analyzeResult.contains("analyze.value")) {
            analyzerFinishedPayload.insert("analyzeValue", analyzeResult.value("analyze.value"));
        }
        if (analyzeResult.contains("analyze.min")) {
            analyzerFinishedPayload.insert("analyzeMin", analyzeResult.value("analyze.min"));
        }
        if (analyzeResult.contains("analyze.max")) {
            analyzerFinishedPayload.insert("analyzeMax", analyzeResult.value("analyze.max"));
        }
        if (analyzeResult.contains("analyze.unit")) {
            analyzerFinishedPayload.insert("analyzeUnit", analyzeResult.value("analyze.unit"));
        }
        if (analyzeResult.contains("analyze.message")) {
            analyzerFinishedPayload.insert("analyzeMessage", analyzeResult.value("analyze.message"));
        }
        eventBus_->publish("analyzer.finished", analyzerFinishedPayload);
    }

    for (auto it = pluginManager_.reporters().cbegin(); it != pluginManager_.reporters().cend(); ++it) {
        const QString reporterId = it.key();
        auto* reporterPlugin = it.value();
        if (reporterPlugin == nullptr) {
            continue;
        }

        eventBus_->publish("reporter.started", {
            {"workflowId", context.workflowId},
            {"reporterId", reporterId}
        });

        QString reportError;
        if (!reporterPlugin->report(context, reportError)) {
            return failWorkflow(
                {
                    {"reporterId", reporterId}
                },
                QString("Reporter '%1' failed: %2").arg(reporterId, reportError)
            );
        }

        eventBus_->publish("reporter.finished", {
            {"workflowId", context.workflowId},
            {"reporterId", reporterId}
        });
    }

    eventBus_->publish("workflow.finished", {
        {"workflowId", context.workflowId},
        {"contextSize", context.data.size()}
    });
    return true;
}

bool WorkflowEngine::executeMinimalWorkflow(QString* errorMessage) {
    return executeWorkflow(eon::domain::createMinimalWorkflowDefinition(), errorMessage);
}

} // namespace eon::runtime
