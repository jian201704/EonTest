#include "eon/infra/TestResultCollector.h"
#include "eon/infra/SqliteTestRepository.h"

#include <QDateTime>

namespace eon::infra {

TestResultCollector::TestResultCollector(eon::core::EventBus* eventBus,
                                          SqliteTestRepository* repository,
                                          QObject* parent)
    : QObject(parent)
    , eventBus_(eventBus)
    , repository_(repository)
{
    if (eventBus_) {
        eventBus_->subscribe("workflow.started", [this](const QVariantMap& p) { onWorkflowStarted(p); });
        eventBus_->subscribe("workflow.finished", [this](const QVariantMap& p) { onWorkflowFinished(p); });
        eventBus_->subscribe("workflow.failed", [this](const QVariantMap& p) { onWorkflowFailed(p); });
        eventBus_->subscribe("activity.started", [this](const QVariantMap& p) { onActivityStarted(p); });
        eventBus_->subscribe("activity.finished", [this](const QVariantMap& p) { onActivityFinished(p); });
        eventBus_->subscribe("activity.failed", [this](const QVariantMap& p) { onActivityFailed(p); });
        eventBus_->subscribe("activity.skipped", [this](const QVariantMap& p) { onActivitySkipped(p); });
        eventBus_->subscribe("compensation.started", [this](const QVariantMap& p) { onCompensationStarted(p); });
        eventBus_->subscribe("compensation.finished", [this](const QVariantMap& p) { onCompensationFinished(p); });
    }
}

TestResultCollector::~TestResultCollector() = default;

void TestResultCollector::reset() {
    currentResult_ = eon::domain::TestResult();
    lastResult_ = eon::domain::TestResult();
    collecting_ = false;
    stepTimers_.clear();
}

void TestResultCollector::onWorkflowStarted(const QVariantMap& payload) {
    reset();

    currentResult_.setWorkflowId(payload.value("workflowId").toString());
    currentResult_.setWorkflowName(payload.value("workflowId").toString());
    currentResult_.setStartedAt(QDateTime::currentDateTimeUtc());
    collecting_ = true;
    elapsed_.start();
}

void TestResultCollector::onWorkflowFinished(const QVariantMap& payload) {
    Q_UNUSED(payload)
    finalizeResult(eon::core::PassFail::Pass);
}

void TestResultCollector::onWorkflowFailed(const QVariantMap& payload) {
    finalizeResult(eon::core::PassFail::Fail, payload.value("error").toString());
}

void TestResultCollector::onActivityStarted(const QVariantMap& payload) {
    if (!collecting_) return;
    const QString stepId = payload.value("stepId").toString();
    stepTimers_[stepId] = elapsed_.elapsed();
}

void TestResultCollector::onActivityFinished(const QVariantMap& payload) {
    if (!collecting_) return;

    eon::domain::StepResult sr;
    sr.stepId = payload.value("stepId").toString();
    sr.pluginId = payload.value("pluginId").toString();
    sr.status = "success";
    sr.attemptCount = payload.value("attempt", 1).toInt();

    const qint64 startMs = stepTimers_.value(sr.stepId, 0);
    sr.elapsedMs = elapsed_.elapsed() - startMs;
    stepTimers_.remove(sr.stepId);

    currentResult_.addStepResult(sr);
}

void TestResultCollector::onActivityFailed(const QVariantMap& payload) {
    if (!collecting_) return;

    eon::domain::StepResult sr;
    sr.stepId = payload.value("stepId").toString();
    sr.pluginId = payload.value("pluginId").toString();

    // 区分 hard fail vs continue_on_error
    const QString policy = payload.value("policy").toString();
    sr.status = (policy == "continue_on_error") ? "continue_on_error" : "failed";

    sr.errorMessage = payload.value("error").toString();

    const qint64 startMs = stepTimers_.value(sr.stepId, 0);
    sr.elapsedMs = elapsed_.elapsed() - startMs;
    stepTimers_.remove(sr.stepId);

    currentResult_.addStepResult(sr);
}

void TestResultCollector::onActivitySkipped(const QVariantMap& payload) {
    if (!collecting_) return;

    eon::domain::StepResult sr;
    sr.stepId = payload.value("stepId").toString();
    sr.pluginId = payload.value("pluginId").toString();
    sr.status = "skipped";

    currentResult_.addStepResult(sr);
}

void TestResultCollector::onCompensationStarted(const QVariantMap& payload) {
    if (!collecting_) return;
    const QString stepId = payload.value("compensationStepId").toString();
    stepTimers_[stepId] = elapsed_.elapsed();
}

void TestResultCollector::onCompensationFinished(const QVariantMap& payload) {
    if (!collecting_) return;

    eon::domain::StepResult sr;
    sr.stepId = payload.value("compensationStepId").toString();
    sr.pluginId = payload.value("pluginId").toString();
    sr.status = "compensated";

    const qint64 startMs = stepTimers_.value(sr.stepId, 0);
    sr.elapsedMs = elapsed_.elapsed() - startMs;
    stepTimers_.remove(sr.stepId);

    currentResult_.addStepResult(sr);
}

void TestResultCollector::finalizeResult(eon::core::PassFail outcome, const QString& error) {
    if (!collecting_) return;

    currentResult_.setFinishedAt(QDateTime::currentDateTimeUtc());
    currentResult_.setTotalElapsedMs(elapsed_.elapsed());
    currentResult_.setOverallResult(outcome);
    currentResult_.setErrorSummary(error);

    collecting_ = false;

    // 持久化
    if (repository_) {
        QString saveError;
        if (repository_->save(currentResult_, saveError)) {
            persistedIds_.append(currentResult_.id());
            qInfo() << "TestResult persisted:" << currentResult_.id()
                    << "outcome:" << eon::core::passFailToString(outcome)
                    << "steps:" << currentResult_.stepResults().size();
        } else {
            qWarning() << "TestResult save failed:" << saveError;
        }
    }

    lastResult_ = currentResult_;
}

} // namespace eon::infra
