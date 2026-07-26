#pragma once

#include <QSqlDatabase>
#include <QString>

#include "eon/domain/TestResult.h"

namespace eon::infra {

// ============================================================================
// SqliteTestRepository — SQLite 测试结果仓储
// 实现 ITestResultRepository，持久化完整测试数据
//
// 特性:
//   - 自动建表（首次打开时迁移）
//   - 事务批量写入
//   - WAL 模式 + 外键
//   - 按时间/工作流/批次索引查询
// ============================================================================
class SqliteTestRepository : public eon::domain::ITestResultRepository {
public:
    /// @param dbPath 数据库文件路径，":memory:" 为内存库
    explicit SqliteTestRepository(const QString& dbPath);
    ~SqliteTestRepository() override;

    // --- ITestResultRepository ---

    bool save(const eon::domain::TestResult& result, QString& errorMessage) override;
    bool findById(const QString& id, eon::domain::TestResult& result,
                  QString& errorMessage) override;
    QList<eon::domain::TestResult> findByWorkflowId(const QString& workflowId,
                                                      int limit = 100,
                                                      QString* errorMessage = nullptr) override;
    QList<eon::domain::TestResult> findByBatchId(const QString& batchId,
                                                   QString* errorMessage = nullptr) override;
    QList<eon::domain::TestResult> findRecent(int limit = 50,
                                                QString* errorMessage = nullptr) override;

    qint64 totalCount(QString* errorMessage = nullptr) override;
    double passRate(const QString& workflowId = {},
                    QString* errorMessage = nullptr) override;

    // --- 扩展查询（UI 面板用） ---

    /// 获取失败率最高的步骤统计
    QList<QVariantMap> stepFailureStats(const QString& workflowId = {},
                                         int limit = 20);

    /// 获取指定时间段内每天的测试统计
    QList<QVariantMap> dailyStats(const QDate& from, const QDate& to);

    /// 清理 N 天前的旧数据
    bool purgeOlderThan(int days, QString& errorMessage);

    /// 数据库是否已打开
    bool isOpen() const;

private:
    bool ensureSchema(QString& errorMessage);
    bool saveStepResults(const QString& testResultId,
                         const QList<eon::domain::StepResult>& steps,
                         QString& errorMessage);
    bool saveMeasurements(const QString& testResultId,
                          const QList<eon::core::Measurement>& measurements,
                          const QList<eon::domain::StepResult>& steps,
                          QString& errorMessage);

    eon::domain::TestResult rowToTestResult(const QSqlQuery& query) const;
    QList<eon::domain::StepResult> loadStepResults(const QString& testResultId) const;
    QList<eon::core::Measurement> loadMeasurements(const QString& testResultId) const;

    QString dbPath_;
    QString connectionName_;
    bool initialized_ = false;
};

} // namespace eon::infra
