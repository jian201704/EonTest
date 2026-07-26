#include "eon/infra/SqliteTestRepository.h"

#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace eon::infra {

SqliteTestRepository::SqliteTestRepository(const QString& dbPath)
    : dbPath_(dbPath)
    , connectionName_(QString("eon-test-results-%1")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)))
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    db.setDatabaseName(dbPath_);

    if (!db.open()) {
        qCritical() << "SqliteTestRepository: Cannot open" << dbPath_ << db.lastError().text();
        return;
    }

    // WAL 模式提升并发读性能
    QSqlQuery pragma(db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA foreign_keys=ON");
    pragma.exec("PRAGMA busy_timeout=5000");

    QString schemaError;
    if (!ensureSchema(schemaError)) {
        qCritical() << "SqliteTestRepository: Schema error:" << schemaError;
        db.close();
        return;
    }

    initialized_ = true;
}

SqliteTestRepository::~SqliteTestRepository() {
    if (QSqlDatabase::contains(connectionName_)) {
        {
            QSqlDatabase db = QSqlDatabase::database(connectionName_);
            if (db.isOpen()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool SqliteTestRepository::isOpen() const {
    return initialized_ && QSqlDatabase::database(connectionName_).isOpen();
}

// ============================================================================
// Schema
// ============================================================================

bool SqliteTestRepository::ensureSchema(QString& errorMessage) {
    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);

    const QStringList ddl = {
        // 测试结果主表
        "CREATE TABLE IF NOT EXISTS test_results ("
        "  id TEXT PRIMARY KEY,"
        "  workflow_id TEXT NOT NULL,"
        "  workflow_name TEXT DEFAULT '',"
        "  batch_id TEXT DEFAULT '',"
        "  cell_id INTEGER DEFAULT 0,"
        "  started_at TEXT NOT NULL,"
        "  finished_at TEXT DEFAULT '',"
        "  total_elapsed_ms INTEGER DEFAULT 0,"
        "  overall_result TEXT DEFAULT 'not_evaluated',"
        "  error_summary TEXT DEFAULT '',"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
        ")",

        // 步骤结果表
        "CREATE TABLE IF NOT EXISTS step_results ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  test_result_id TEXT NOT NULL,"
        "  step_id TEXT NOT NULL,"
        "  plugin_id TEXT DEFAULT '',"
        "  status TEXT NOT NULL,"
        "  attempt_count INTEGER DEFAULT 1,"
        "  elapsed_ms INTEGER DEFAULT 0,"
        "  error_message TEXT DEFAULT '',"
        "  output_data TEXT DEFAULT '{}',"
        "  FOREIGN KEY(test_result_id) REFERENCES test_results(id) ON DELETE CASCADE"
        ")",

        // 测量值表
        "CREATE TABLE IF NOT EXISTS measurements ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  test_result_id TEXT NOT NULL,"
        "  step_id TEXT DEFAULT '',"
        "  name TEXT NOT NULL,"
        "  value REAL NOT NULL,"
        "  unit TEXT DEFAULT '',"
        "  timestamp_us INTEGER DEFAULT 0,"
        "  FOREIGN KEY(test_result_id) REFERENCES test_results(id) ON DELETE CASCADE"
        ")",

        // 索引
        "CREATE INDEX IF NOT EXISTS idx_tr_workflow ON test_results(workflow_id)",
        "CREATE INDEX IF NOT EXISTS idx_tr_batch ON test_results(batch_id)",
        "CREATE INDEX IF NOT EXISTS idx_tr_created ON test_results(created_at DESC)",
        "CREATE INDEX IF NOT EXISTS idx_tr_result ON test_results(overall_result)",
        "CREATE INDEX IF NOT EXISTS idx_sr_tr ON step_results(test_result_id)",
        "CREATE INDEX IF NOT EXISTS idx_sr_step ON step_results(step_id, status)",
        "CREATE INDEX IF NOT EXISTS idx_ms_tr ON measurements(test_result_id)",
        "CREATE INDEX IF NOT EXISTS idx_ms_name ON measurements(name)",
    };

    for (const QString& stmt : ddl) {
        if (!q.exec(stmt)) {
            errorMessage = q.lastError().text();
            return false;
        }
    }

    return true;
}

// ============================================================================
// 保存
// ============================================================================

bool SqliteTestRepository::save(const eon::domain::TestResult& result,
                                 QString& errorMessage) {
    if (!initialized_) {
        errorMessage = "Repository not initialized.";
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    if (!db.transaction()) {
        errorMessage = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    q.prepare(
        "INSERT OR REPLACE INTO test_results"
        "(id, workflow_id, workflow_name, batch_id, cell_id,"
        " started_at, finished_at, total_elapsed_ms,"
        " overall_result, error_summary)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );

    q.addBindValue(result.id());
    q.addBindValue(result.workflowId());
    q.addBindValue(result.workflowName());
    q.addBindValue(result.batchId());
    q.addBindValue(result.cellId());
    q.addBindValue(result.startedAt().toString(Qt::ISODateWithMs));
    q.addBindValue(result.finishedAt().toString(Qt::ISODateWithMs));
    q.addBindValue(result.totalElapsedMs());
    q.addBindValue(eon::core::passFailToString(result.overallResult()));
    q.addBindValue(result.errorSummary());

    if (!q.exec()) {
        errorMessage = q.lastError().text();
        db.rollback();
        return false;
    }

    // 步骤结果
    if (!saveStepResults(result.id(), result.stepResults(), errorMessage)) {
        db.rollback();
        return false;
    }

    // 测量值
    if (!saveMeasurements(result.id(), result.measurements(),
                          result.stepResults(), errorMessage)) {
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        errorMessage = db.lastError().text();
        db.rollback();
        return false;
    }

    return true;
}

bool SqliteTestRepository::saveStepResults(const QString& testResultId,
                                            const QList<eon::domain::StepResult>& steps,
                                            QString& errorMessage) {
    QSqlDatabase db = QSqlDatabase::database(connectionName_);

    // 先删除该 test result 的旧步骤结果
    QSqlQuery del(db);
    del.prepare("DELETE FROM step_results WHERE test_result_id = ?");
    del.addBindValue(testResultId);
    del.exec();

    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO step_results"
        "(test_result_id, step_id, plugin_id, status, attempt_count, elapsed_ms,"
        " error_message, output_data)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?)"
    );

    for (const auto& step : steps) {
        q.addBindValue(testResultId);
        q.addBindValue(step.stepId);
        q.addBindValue(step.pluginId);
        q.addBindValue(step.status);
        q.addBindValue(step.attemptCount);
        q.addBindValue(step.elapsedMs);
        q.addBindValue(step.errorMessage);
        q.addBindValue(QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(step.outputData)).toJson(QJsonDocument::Compact)));

        if (!q.exec()) {
            errorMessage = q.lastError().text();
            return false;
        }
    }

    return true;
}

bool SqliteTestRepository::saveMeasurements(const QString& testResultId,
                                             const QList<eon::core::Measurement>& measurements,
                                             const QList<eon::domain::StepResult>& steps,
                                             QString& errorMessage) {
    if (measurements.isEmpty()) return true;

    QSqlDatabase db = QSqlDatabase::database(connectionName_);

    QSqlQuery del(db);
    del.prepare("DELETE FROM measurements WHERE test_result_id = ?");
    del.addBindValue(testResultId);
    del.exec();

    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO measurements(test_result_id, step_id, name, value, unit, timestamp_us)"
        " VALUES(?, ?, ?, ?, ?, ?)"
    );

    for (const auto& m : measurements) {
        q.addBindValue(testResultId);
        // 测量值关联到最近的步骤
        q.addBindValue(steps.isEmpty() ? QString() : steps.last().stepId);
        q.addBindValue(m.name());
        q.addBindValue(m.value());
        q.addBindValue(m.unit());
        q.addBindValue(static_cast<qint64>(m.timestampUs()));

        if (!q.exec()) {
            errorMessage = q.lastError().text();
            return false;
        }
    }

    return true;
}

// ============================================================================
// 查询
// ============================================================================

bool SqliteTestRepository::findById(const QString& id, eon::domain::TestResult& result,
                                     QString& errorMessage) {
    if (!initialized_) {
        errorMessage = "Repository not initialized.";
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM test_results WHERE id = ?");
    q.addBindValue(id);

    if (!q.exec() || !q.next()) {
        errorMessage = q.lastError().text().isEmpty() ? "Not found." : q.lastError().text();
        return false;
    }

    result = rowToTestResult(q);
    result.setStepResults(loadStepResults(id));
    result.setMeasurements(loadMeasurements(id));
    return true;
}

QList<eon::domain::TestResult> SqliteTestRepository::findByWorkflowId(
    const QString& workflowId, int limit, QString* errorMessage) {
    QList<eon::domain::TestResult> results;
    if (!initialized_) {
        if (errorMessage) *errorMessage = "Repository not initialized.";
        return results;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);
    q.prepare(
        "SELECT * FROM test_results WHERE workflow_id = ?"
        " ORDER BY created_at DESC LIMIT ?"
    );
    q.addBindValue(workflowId);
    q.addBindValue(limit);

    if (!q.exec()) {
        if (errorMessage) *errorMessage = q.lastError().text();
        return results;
    }

    while (q.next()) {
        results.append(rowToTestResult(q));
    }
    return results;
}

QList<eon::domain::TestResult> SqliteTestRepository::findByBatchId(
    const QString& batchId, QString* errorMessage) {
    QList<eon::domain::TestResult> results;
    if (!initialized_) {
        if (errorMessage) *errorMessage = "Repository not initialized.";
        return results;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM test_results WHERE batch_id = ? ORDER BY created_at DESC");
    q.addBindValue(batchId);

    if (!q.exec()) {
        if (errorMessage) *errorMessage = q.lastError().text();
        return results;
    }

    while (q.next()) {
        results.append(rowToTestResult(q));
    }
    return results;
}

QList<eon::domain::TestResult> SqliteTestRepository::findRecent(int limit,
                                                                 QString* errorMessage) {
    QList<eon::domain::TestResult> results;
    if (!initialized_) {
        if (errorMessage) *errorMessage = "Repository not initialized.";
        return results;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM test_results ORDER BY created_at DESC LIMIT ?");
    q.addBindValue(limit);

    if (!q.exec()) {
        if (errorMessage) *errorMessage = q.lastError().text();
        return results;
    }

    while (q.next()) {
        results.append(rowToTestResult(q));
    }
    return results;
}

// ============================================================================
// 统计
// ============================================================================

qint64 SqliteTestRepository::totalCount(QString* errorMessage) {
    if (!initialized_) {
        if (errorMessage) *errorMessage = "Repository not initialized.";
        return -1;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);
    if (!q.exec("SELECT COUNT(*) FROM test_results")) {
        if (errorMessage) *errorMessage = q.lastError().text();
        return -1;
    }

    return q.next() ? q.value(0).toLongLong() : 0;
}

double SqliteTestRepository::passRate(const QString& workflowId, QString* errorMessage) {
    if (!initialized_) {
        if (errorMessage) *errorMessage = "Repository not initialized.";
        return -1.0;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);

    QString sql = "SELECT COUNT(*) FROM test_results";
    if (!workflowId.isEmpty()) {
        sql += " WHERE workflow_id = ?";
        q.prepare(sql);
        q.addBindValue(workflowId);
    } else {
        q.prepare(sql);
    }

    if (!q.exec() || !q.next()) {
        if (errorMessage) *errorMessage = q.lastError().text();
        return -1.0;
    }

    const qint64 total = q.value(0).toLongLong();
    if (total == 0) return 0.0;

    QString passSql = "SELECT COUNT(*) FROM test_results WHERE overall_result = 'pass'";
    if (!workflowId.isEmpty()) {
        passSql += " AND workflow_id = ?";
    }

    QSqlQuery q2(db);
    q2.prepare(passSql);
    if (!workflowId.isEmpty()) {
        q2.addBindValue(workflowId);
    }

    if (!q2.exec() || !q2.next()) {
        if (errorMessage) *errorMessage = q2.lastError().text();
        return -1.0;
    }

    return static_cast<double>(q2.value(0).toLongLong()) / static_cast<double>(total) * 100.0;
}

// ============================================================================
// 扩展查询
// ============================================================================

QList<QVariantMap> SqliteTestRepository::stepFailureStats(const QString& workflowId, int limit) {
    QList<QVariantMap> stats;
    if (!initialized_) return stats;

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);

    QString sql =
        "SELECT sr.step_id, sr.plugin_id, COUNT(*) AS fail_count"
        " FROM step_results sr"
        " JOIN test_results tr ON sr.test_result_id = tr.id"
        " WHERE sr.status = 'failed'";

    if (!workflowId.isEmpty()) {
        sql += " AND tr.workflow_id = ?";
    }
    sql += " GROUP BY sr.step_id, sr.plugin_id ORDER BY fail_count DESC LIMIT ?";

    q.prepare(sql);
    if (!workflowId.isEmpty()) {
        q.addBindValue(workflowId);
    }
    q.addBindValue(limit);

    if (!q.exec()) return stats;

    while (q.next()) {
        QVariantMap row;
        row["stepId"] = q.value("step_id").toString();
        row["pluginId"] = q.value("plugin_id").toString();
        row["failCount"] = q.value("fail_count").toInt();
        stats.append(row);
    }
    return stats;
}

QList<QVariantMap> SqliteTestRepository::dailyStats(const QDate& from, const QDate& to) {
    QList<QVariantMap> stats;
    if (!initialized_) return stats;

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);
    q.prepare(
        "SELECT DATE(created_at) AS day,"
        " COUNT(*) AS total,"
        " SUM(CASE WHEN overall_result = 'pass' THEN 1 ELSE 0 END) AS passed,"
        " SUM(CASE WHEN overall_result = 'fail' THEN 1 ELSE 0 END) AS failed,"
        " AVG(total_elapsed_ms) AS avg_elapsed_ms"
        " FROM test_results"
        " WHERE created_at >= ? AND created_at <= ?"
        " GROUP BY day ORDER BY day ASC"
    );

    q.addBindValue(from.toString(Qt::ISODate));
    q.addBindValue(to.addDays(1).toString(Qt::ISODate)); // 包含 to 当天

    if (!q.exec()) return stats;

    while (q.next()) {
        QVariantMap row;
        row["day"] = q.value("day").toString();
        row["total"] = q.value("total").toInt();
        row["passed"] = q.value("passed").toInt();
        row["failed"] = q.value("failed").toInt();
        row["avgElapsedMs"] = q.value("avg_elapsed_ms").toDouble();
        stats.append(row);
    }
    return stats;
}

bool SqliteTestRepository::purgeOlderThan(int days, QString& errorMessage) {
    if (!initialized_) {
        errorMessage = "Repository not initialized.";
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);
    q.prepare("DELETE FROM test_results WHERE created_at < datetime('now', ? || ' days')");
    q.addBindValue(QString("-%1").arg(days));

    if (!q.exec()) {
        errorMessage = q.lastError().text();
        return false;
    }

    qInfo() << "SqliteTestRepository: Purged" << q.numRowsAffected()
            << "records older than" << days << "days.";
    return true;
}

// ============================================================================
// 内部映射
// ============================================================================

eon::domain::TestResult SqliteTestRepository::rowToTestResult(const QSqlQuery& query) const {
    eon::domain::TestResult result;
    result.setId(query.value("id").toString());
    result.setWorkflowId(query.value("workflow_id").toString());
    result.setWorkflowName(query.value("workflow_name").toString());
    result.setBatchId(query.value("batch_id").toString());
    result.setCellId(query.value("cell_id").toInt());
    result.setStartedAt(QDateTime::fromString(query.value("started_at").toString(), Qt::ISODateWithMs));
    result.setFinishedAt(QDateTime::fromString(query.value("finished_at").toString(), Qt::ISODateWithMs));
    result.setTotalElapsedMs(query.value("total_elapsed_ms").toLongLong());
    result.setOverallResult(eon::core::passFailFromString(query.value("overall_result").toString()));
    result.setErrorSummary(query.value("error_summary").toString());
    return result;
}

QList<eon::domain::StepResult> SqliteTestRepository::loadStepResults(
    const QString& testResultId) const {
    QList<eon::domain::StepResult> steps;

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM step_results WHERE test_result_id = ? ORDER BY id ASC");
    q.addBindValue(testResultId);

    if (!q.exec()) return steps;

    while (q.next()) {
        eon::domain::StepResult sr;
        sr.stepId = q.value("step_id").toString();
        sr.pluginId = q.value("plugin_id").toString();
        sr.status = q.value("status").toString();
        sr.attemptCount = q.value("attempt_count").toInt();
        sr.elapsedMs = q.value("elapsed_ms").toLongLong();
        sr.errorMessage = q.value("error_message").toString();

        const QJsonDocument doc = QJsonDocument::fromJson(q.value("output_data").toString().toUtf8());
        if (doc.isObject()) {
            sr.outputData = doc.object().toVariantMap();
        }

        steps.append(sr);
    }
    return steps;
}

QList<eon::core::Measurement> SqliteTestRepository::loadMeasurements(
    const QString& testResultId) const {
    QList<eon::core::Measurement> measurements;

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    QSqlQuery q(db);
    q.prepare("SELECT * FROM measurements WHERE test_result_id = ? ORDER BY id ASC");
    q.addBindValue(testResultId);

    if (!q.exec()) return measurements;

    while (q.next()) {
        measurements.append(eon::core::Measurement(
            q.value("name").toString(),
            q.value("value").toDouble(),
            q.value("unit").toString(),
            static_cast<quint64>(q.value("timestamp_us").toLongLong())
        ));
    }
    return measurements;
}

} // namespace eon::infra
