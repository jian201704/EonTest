#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QVariantMap>

#include "eon/core/Entity.h"

namespace eon::domain {

// ============================================================================
// StepResult — 单个步骤执行结果
// ============================================================================
struct StepResult {
    QString stepId;
    QString pluginId;
    QString status;        // "success" | "failed" | "skipped" | "compensated"
    int attemptCount = 1;
    qint64 elapsedMs = 0;
    QString errorMessage;
    QVariantMap outputData;
};

// ============================================================================
// TestResult — 测试结果聚合根
// 一次工作流执行的完整测试结果
// ============================================================================
class TestResult : public eon::core::Entity {
public:
    TestResult() = default;

    // --- 基本属性 ---
    QString workflowId() const { return workflowId_; }
    void setWorkflowId(const QString& id) { workflowId_ = id; }

    QString workflowName() const { return workflowName_; }
    void setWorkflowName(const QString& name) { workflowName_ = name; }

    QString batchId() const { return batchId_; }
    void setBatchId(const QString& id) { batchId_ = id; }

    int cellId() const { return cellId_; }
    void setCellId(int id) { cellId_ = id; }

    QString dutId() const { return dutId_; }
    void setDutId(const QString& id) { dutId_ = id; }

    // --- 执行信息 ---
    QDateTime startedAt() const { return startedAt_; }
    void setStartedAt(const QDateTime& dt) { startedAt_ = dt; }

    QDateTime finishedAt() const { return finishedAt_; }
    void setFinishedAt(const QDateTime& dt) { finishedAt_ = dt; }

    qint64 totalElapsedMs() const { return totalElapsedMs_; }
    void setTotalElapsedMs(qint64 ms) { totalElapsedMs_ = ms; }

    // --- 判定 ---
    eon::core::PassFail overallResult() const { return overallResult_; }
    void setOverallResult(eon::core::PassFail result) { overallResult_ = result; }

    QString errorSummary() const { return errorSummary_; }
    void setErrorSummary(const QString& summary) { errorSummary_ = summary; }

    // --- 步骤结果 ---
    QList<StepResult> stepResults() const { return stepResults_; }
    void addStepResult(const StepResult& sr) { stepResults_.append(sr); }
    void setStepResults(const QList<StepResult>& results) { stepResults_ = results; }

    int failedStepCount() const {
        int count = 0;
        for (const auto& sr : stepResults_) {
            if (sr.status == "failed") count++;
        }
        return count;
    }

    // --- 测量值 ---
    QList<eon::core::Measurement> measurements() const { return measurements_; }
    void addMeasurement(const eon::core::Measurement& m) { measurements_.append(m); }
    void setMeasurements(const QList<eon::core::Measurement>& m) { measurements_ = m; }

    // --- 序列化 ---
    QVariantMap toVariantMap() const {
        QVariantMap map;
        map["id"] = id();
        map["workflowId"] = workflowId_;
        map["workflowName"] = workflowName_;
        map["batchId"] = batchId_;
        map["cellId"] = cellId_;        map["dutId"] = dutId_;        map["startedAt"] = startedAt_.toString(Qt::ISODateWithMs);
        map["finishedAt"] = finishedAt_.toString(Qt::ISODateWithMs);
        map["totalElapsedMs"] = totalElapsedMs_;
        map["overallResult"] = eon::core::passFailToString(overallResult_);
        map["errorSummary"] = errorSummary_;
        map["failedStepCount"] = failedStepCount();

        QVariantList stepList;
        for (const auto& sr : stepResults_) {
            QVariantMap sm;
            sm["stepId"] = sr.stepId;
            sm["pluginId"] = sr.pluginId;
            sm["status"] = sr.status;
            sm["attemptCount"] = sr.attemptCount;
            sm["elapsedMs"] = sr.elapsedMs;
            sm["errorMessage"] = sr.errorMessage;
            sm["outputData"] = sr.outputData;
            stepList.append(sm);
        }
        map["stepResults"] = stepList;

        QVariantList measList;
        for (const auto& m : measurements_) {
            measList.append(m.toVariantMap());
        }
        map["measurements"] = measList;

        return map;
    }

private:
    QString workflowId_;
    QString workflowName_;
    QString batchId_;
    int cellId_ = 0;
    QString dutId_;

    QDateTime startedAt_;
    QDateTime finishedAt_;
    qint64 totalElapsedMs_ = 0;

    eon::core::PassFail overallResult_ = eon::core::PassFail::NotEvaluated;
    QString errorSummary_;

    QList<StepResult> stepResults_;
    QList<eon::core::Measurement> measurements_;
};

// ============================================================================
// ITestResultRepository — 测试结果仓储接口
// ============================================================================
class ITestResultRepository {
public:
    virtual ~ITestResultRepository() = default;

    virtual bool save(const TestResult& result, QString& errorMessage) = 0;
    virtual bool findById(const QString& id, TestResult& result, QString& errorMessage) = 0;
    virtual QList<TestResult> findByWorkflowId(const QString& workflowId, int limit = 100,
                                                QString* errorMessage = nullptr) = 0;
    virtual QList<TestResult> findByBatchId(const QString& batchId, QString* errorMessage = nullptr) = 0;
    virtual QList<TestResult> findRecent(int limit = 50, QString* errorMessage = nullptr) = 0;

    virtual qint64 totalCount(QString* errorMessage = nullptr) = 0;
    virtual double passRate(const QString& workflowId = {}, QString* errorMessage = nullptr) = 0;
};

} // namespace eon::domain
