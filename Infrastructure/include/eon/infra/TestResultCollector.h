#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>

#include "eon/core/EventBus.h"
#include "eon/domain/TestResult.h"

namespace eon::infra {

class SqliteTestRepository;

// ============================================================================
// TestResultCollector — 通过 EventBus 收集测试结果并持久化
//
// 监听 workflow/activity 事件，自动构建 TestResult 聚合根，
// 在 workflow 完成/失败时写入 SqliteTestRepository。
//
// 使用方式:
//   TestResultCollector collector(&eventBus, repo);
//   // ... 执行 workflow ...
//   collector.flush(); // workflow 结束后查询结果
// ============================================================================
class TestResultCollector : public QObject {
    Q_OBJECT

public:
    /// @param eventBus 事件总线（必须与 WorkflowEngine 共用同一个）
    /// @param repository 测试结果仓储
    TestResultCollector(eon::core::EventBus* eventBus,
                        SqliteTestRepository* repository,
                        QObject* parent = nullptr);
    ~TestResultCollector() override;

    /// 获取最近一次收集的 TestResult
    eon::domain::TestResult lastResult() const { return lastResult_; }

    /// 获取所有已持久化的 TestResult ID 列表
    QStringList persistedIds() const { return persistedIds_; }

    /// 重置收集器状态（用于新一轮测试）
    void reset();

private:
    void onWorkflowStarted(const QVariantMap& payload);
    void onWorkflowFinished(const QVariantMap& payload);
    void onWorkflowFailed(const QVariantMap& payload);
    void onActivityStarted(const QVariantMap& payload);
    void onActivityFinished(const QVariantMap& payload);
    void onActivityFailed(const QVariantMap& payload);
    void onActivitySkipped(const QVariantMap& payload);
    void onCompensationStarted(const QVariantMap& payload);
    void onCompensationFinished(const QVariantMap& payload);

    void finalizeResult(eon::core::PassFail outcome, const QString& error = {});

    eon::core::EventBus* eventBus_ = nullptr;
    SqliteTestRepository* repository_ = nullptr;

    // 当前正在构建的 TestResult
    eon::domain::TestResult currentResult_;
    bool collecting_ = false;
    QElapsedTimer elapsed_;
    QHash<QString, qint64> stepTimers_;  // stepId -> startedAt (elapsed stamp)

    // 已完成的结果
    eon::domain::TestResult lastResult_;
    QStringList persistedIds_;
};

} // namespace eon::infra
