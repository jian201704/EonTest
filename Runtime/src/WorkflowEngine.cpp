#include "eon/runtime/WorkflowEngine.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfoList>
#include <QHash>
#include <QJsonObject>
#include <QSet>

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
    : eventBus_(eventBus) {}

bool WorkflowEngine::loadPlugins(const QString& pluginDirectory, QString* errorMessage) {
    plugins_.clear();
    stepPluginsById_.clear();
    analyzerPluginsById_.clear();
    reporterPluginsById_.clear();

    const QDir directory(pluginDirectory);
    if (!directory.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Plugin directory does not exist: %1").arg(pluginDirectory);
        }
        return false;
    }

    const QFileInfoList files = directory.entryInfoList(QDir::Files);
    for (const QFileInfo& file : files) {
        auto loader = std::make_unique<QPluginLoader>(file.absoluteFilePath());
        QObject* instance = loader->instance();
        if (instance == nullptr) {
            continue;
        }

        const QJsonObject metadata = loader->metaData().value("MetaData").toObject();

        QString contractType;
        QString pluginId;

        if (auto* stepPlugin = qobject_cast<eon::sdk::IStepPlugin*>(instance); stepPlugin != nullptr) {
            contractType = "step";
            pluginId = stepPlugin->id();
            if (stepPluginsById_.contains(pluginId)) {
                continue;
            }
            stepPluginsById_.insert(pluginId, stepPlugin);
        } else if (auto* analyzerPlugin = qobject_cast<eon::sdk::IAnalyzerPlugin*>(instance); analyzerPlugin != nullptr) {
            contractType = "analyzer";
            pluginId = analyzerPlugin->id();
            if (analyzerPluginsById_.contains(pluginId)) {
                continue;
            }
            analyzerPluginsById_.insert(pluginId, analyzerPlugin);
        } else if (auto* reporterPlugin = qobject_cast<eon::sdk::IReporterPlugin*>(instance); reporterPlugin != nullptr) {
            contractType = "reporter";
            pluginId = reporterPlugin->id();
            if (reporterPluginsById_.contains(pluginId)) {
                continue;
            }
            reporterPluginsById_.insert(pluginId, reporterPlugin);
        } else {
            continue;
        }

        const QString declaredContractType = pluginMetaString(metadata, "contractType");
        const QString declaredContractVersion = pluginMetaString(metadata, "contractVersion");

        if (declaredContractType != contractType || declaredContractVersion != kSupportedContractVersion) {
            if (contractType == "step") {
                stepPluginsById_.remove(pluginId);
            } else if (contractType == "analyzer") {
                analyzerPluginsById_.remove(pluginId);
            } else if (contractType == "reporter") {
                reporterPluginsById_.remove(pluginId);
            }
            continue;
        }

        const QString declaredPluginId = pluginMetaString(metadata, "pluginId");
        if (!declaredPluginId.isEmpty() && declaredPluginId != pluginId) {
            if (contractType == "step") {
                stepPluginsById_.remove(pluginId);
            } else if (contractType == "analyzer") {
                analyzerPluginsById_.remove(pluginId);
            } else if (contractType == "reporter") {
                reporterPluginsById_.remove(pluginId);
            }
            continue;
        }

        plugins_.push_back(PluginHandle{
            std::move(loader),
            pluginId,
            contractType,
            declaredContractVersion
        });
    }

    if (stepPluginsById_.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("No compatible plugins loaded from: %1").arg(pluginDirectory);
        }
        return false;
    }

    return true;
}

eon::sdk::IStepPlugin* WorkflowEngine::findStepPluginById(const QString& pluginId) const {
    return stepPluginsById_.value(pluginId, nullptr);
}

bool WorkflowEngine::executeWorkflow(const eon::domain::WorkflowDefinition& workflow, QString* errorMessage) {
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

    eventBus_->publish("workflow.started", {
        {"workflowId", context.workflowId}
    });

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
            const bool pluginSucceeded = plugin->executeStep(context, pluginError);
            const qint64 elapsedMs = timer.elapsed();
            const bool timedOut = step->policy.timeoutMs > 0 && elapsedMs > step->policy.timeoutMs;

            if (pluginSucceeded && !timedOut) {
                eventBus_->publish("activity.finished", {
                    {"stepId", step->stepId},
                    {"pluginId", step->pluginId},
                    {"attempt", attempt},
                    {"elapsedMs", elapsedMs}
                });
                executedStepIds.append(step->stepId);
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

                bool hardFailedInBatch = false;
                QString batchFailureMessage;
                for (const auto* batchStep : groupSteps) {
                    QString stepFailureMessage;
                    const StepRunOutcome outcome = runStep(batchStep, &stepFailureMessage);
                    if (outcome == StepRunOutcome::HardFailed) {
                        hardFailedInBatch = true;
                        batchFailureMessage = stepFailureMessage;
                        break;
                    }
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

        QString stepFailureMessage;
        const StepRunOutcome outcome = runStep(step, &stepFailureMessage);
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

    for (auto it = analyzerPluginsById_.cbegin(); it != analyzerPluginsById_.cend(); ++it) {
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

        eventBus_->publish("analyzer.finished", {
            {"workflowId", context.workflowId},
            {"analyzerId", analyzerId},
            {"resultSize", analyzeResult.size()}
        });
    }

    for (auto it = reporterPluginsById_.cbegin(); it != reporterPluginsById_.cend(); ++it) {
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
