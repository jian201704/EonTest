#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QHash>
#include <QVariantList>
#include <QStringList>
#include <QTimer>

#include "eon/studio/WorkflowEditorModel.h"

namespace eon::studio {

// ============================================================================
// ResourceHoldingModel — 资源持有情况（P1 并行看板）
// ============================================================================
class ResourceHoldingModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { ResourceIdRole = Qt::UserRole + 1, OwnerCellRole, ModeRole, ConflictRole };
    explicit ResourceHoldingModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void update(const QList<QJsonObject>& holdings);
    void clear();
private:
    QList<QJsonObject> holdings_;
};

// ============================================================================
// CapabilityListModel — 插件能力列表（P1 并行看板）
// ============================================================================
class CapabilityListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { PluginIdRole = Qt::UserRole + 1, VersionRole, CapabilitiesRole };
    explicit CapabilityListModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void update(const QList<QJsonObject>& plugins);
    void clear();
private:
    QList<QJsonObject> plugins_;
};

// ============================================================================
// TaskInfo — 轻量任务状态（供 QML 展示）
// ============================================================================
struct TaskInfo {
    int taskId = 0;
    int cellId = -1;             // 所属 CELL 编号，-1 表示未分配
    QString workflowId;
    QString workflowPath;
    QString status = "pending";   // pending/running/succeeded/failed/skipped
    int attempt = 0;
    int maxAttempts = 0;
    int priority = 0;
    int exitCode = 0;
    QString lastError;

    // 测量/判定结果（用于 CELL/Test 视图）
    QString measurementName;
    QString measuredValue;
    QString measuredUnit;
    QVariantList resultItems;
    QString lowerLimit;
    QString upperLimit;
    QString resultText;
    QString analyzeMessage;
    double elapsedMs = 0.0;
};

// ============================================================================
// TelemetrySummary — 遥测摘要
// ============================================================================
struct TelemetrySummary {
    int workflowsStarted = 0;
    int workflowsFinished = 0;
    int workflowsFailed = 0;
    int activitiesStarted = 0;
    int activitiesFinished = 0;
    int activitiesFailed = 0;
    int activitiesSkipped = 0;
    int retries = 0;
    int compensationsStarted = 0;
    int compensationsFinished = 0;
    int compensationsFailed = 0;
    int analyzersStarted = 0;
    int analyzersFinished = 0;
    int reportersStarted = 0;
    int reportersFinished = 0;
    int tasksStarted = 0;
    int tasksFinished = 0;

    void reset() { *this = TelemetrySummary{}; }
};

// ============================================================================
// TaskListModel — 任务列表 QAbstractListModel
// ============================================================================
class TaskListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        TaskIdRole = Qt::UserRole + 1,
        CellIdRole, WorkflowIdRole, WorkflowPathRole, StatusRole,
        AttemptRole, MaxAttemptsRole, PriorityRole,
        ExitCodeRole, LastErrorRole,
        MeasurementNameRole, MeasuredValueRole, MeasuredUnitRole,
        ResultItemsRole,
        LowerLimitRole, UpperLimitRole, ResultTextRole, AnalyzeMessageRole,
        ElapsedMsRole
    };

    explicit TaskListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void updateTask(const TaskInfo& task);
    void clearCellTasks(int cellId);
    void clear();

private:
    QList<TaskInfo> tasks_;
    QHash<int, TaskInfo*> taskMap_;
};

// ============================================================================
// StudioBackend — Studio 主后端（QML 可访问）
//
// 职责:
//   - 管理 Orchestrator 子进程
//   - 解析遥测 JSON 行 → 更新 models
//   - 暴露配置属性给 QML 绑定
//   - 硬件状态查询
// ============================================================================
class StudioBackend : public QObject {
    Q_OBJECT

    // --- QML 可绑定属性 ---
    Q_PROPERTY(QString pluginDirectory READ pluginDirectory WRITE setPluginDirectory NOTIFY pluginDirectoryChanged)
    Q_PROPERTY(QString workflowDirectory READ workflowDirectory WRITE setWorkflowDirectory NOTIFY workflowDirectoryChanged)
    Q_PROPERTY(QString reportDirectory READ reportDirectory WRITE setReportDirectory NOTIFY reportDirectoryChanged)
    Q_PROPERTY(QString stateFilePath READ stateFilePath WRITE setStateFilePath NOTIFY stateFilePathChanged)
    Q_PROPERTY(int cellCount READ cellCount WRITE setCellCount NOTIFY cellCountChanged)
    Q_PROPERTY(QVariantList cellIds READ cellIds NOTIFY cellCountChanged)
    Q_PROPERTY(bool stopOnFailure READ stopOnFailure WRITE setStopOnFailure NOTIFY stopOnFailureChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QStringList workflowPaths READ workflowPaths NOTIFY workflowPathsChanged)
    Q_PROPERTY(QStringList selectedWorkflows READ selectedWorkflows WRITE setSelectedWorkflows NOTIFY selectedWorkflowsChanged)
    Q_PROPERTY(QObject* taskListModel READ taskListModel CONSTANT)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)
    Q_PROPERTY(QString logHtml READ logHtml NOTIFY logTextChanged)

    // 遥测计数器
    Q_PROPERTY(int cntWorkflowsStarted READ cntWorkflowsStarted NOTIFY telemetryUpdated)
    Q_PROPERTY(int cntWorkflowsFinished READ cntWorkflowsFinished NOTIFY telemetryUpdated)
    Q_PROPERTY(int cntWorkflowsFailed READ cntWorkflowsFailed NOTIFY telemetryUpdated)
    Q_PROPERTY(int cntActivitiesStarted READ cntActivitiesStarted NOTIFY telemetryUpdated)
    Q_PROPERTY(int cntActivitiesFinished READ cntActivitiesFinished NOTIFY telemetryUpdated)
    Q_PROPERTY(int cntActivitiesFailed READ cntActivitiesFailed NOTIFY telemetryUpdated)
    Q_PROPERTY(int cntActivitiesSkipped READ cntActivitiesSkipped NOTIFY telemetryUpdated)
    Q_PROPERTY(int cntRetries READ cntRetries NOTIFY telemetryUpdated)
    Q_PROPERTY(int cntTasksStarted READ cntTasksStarted NOTIFY telemetryUpdated)
    Q_PROPERTY(int cntTasksFinished READ cntTasksFinished NOTIFY telemetryUpdated)
    Q_PROPERTY(double passRate READ passRate NOTIFY telemetryUpdated)
    Q_PROPERTY(int totalLogLines READ totalLogLines NOTIFY logTextChanged)

    // --- P1 并行看板属性 ---
    Q_PROPERTY(int healthyCellCount READ healthyCellCount NOTIFY dashboardUpdated)
    Q_PROPERTY(int runningTaskCount READ runningTaskCount NOTIFY dashboardUpdated)
    Q_PROPERTY(int failedTaskCount READ failedTaskCount NOTIFY dashboardUpdated)
    Q_PROPERTY(int resourceConflictCount READ resourceConflictCount NOTIFY dashboardUpdated)
    Q_PROPERTY(QObject* resourceHoldingModel READ resourceHoldingModel CONSTANT)
    Q_PROPERTY(QObject* capabilityRegistryModel READ capabilityRegistryModel CONSTANT)

    // --- 每CELL独立状态属性 (CellPanel 绑定) ---
    Q_PROPERTY(QVariantList cellConfigs READ cellConfigs NOTIFY cellConfigsChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticationChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY authenticationChanged)
    Q_PROPERTY(QString userRole READ userRole NOTIFY authenticationChanged)
    Q_PROPERTY(bool canConfigure READ canConfigure NOTIFY authenticationChanged)

public:
    explicit StudioBackend(QObject* parent = nullptr);
    ~StudioBackend() override;

    // --- 属性访问器 ---
    QString pluginDirectory() const;
    void setPluginDirectory(const QString& dir);
    QString workflowDirectory() const;
    void setWorkflowDirectory(const QString& dir);
    QString reportDirectory() const;
    void setReportDirectory(const QString& dir);
    QString stateFilePath() const;
    void setStateFilePath(const QString& path);
    int cellCount() const;
    void setCellCount(int count);
    QVariantList cellIds() const;
    bool authenticated() const;
    QString userName() const;
    QString userRole() const;
    bool canConfigure() const;
    bool stopOnFailure() const;
    void setStopOnFailure(bool stop);
    bool isRunning() const;
    QString statusText() const;
    QStringList workflowPaths() const;
    QStringList selectedWorkflows() const;
    void setSelectedWorkflows(const QStringList& paths);
    QObject* taskListModel() { return &taskListModel_; }
    QString logText() const;
    QString logHtml() const { return logHtml_; }

    // --- 遥测计数器 ---
    int cntWorkflowsStarted() const { return telemetry_.workflowsStarted; }
    int cntWorkflowsFinished() const { return telemetry_.workflowsFinished; }
    int cntWorkflowsFailed() const { return telemetry_.workflowsFailed; }
    int cntActivitiesStarted() const { return telemetry_.activitiesStarted; }
    int cntActivitiesFinished() const { return telemetry_.activitiesFinished; }
    int cntActivitiesFailed() const { return telemetry_.activitiesFailed; }
    int cntActivitiesSkipped() const { return telemetry_.activitiesSkipped; }
    int cntRetries() const { return telemetry_.retries; }
    int cntTasksStarted() const { return telemetry_.tasksStarted; }
    int cntTasksFinished() const { return telemetry_.tasksFinished; }
    double passRate() const { return passRate_; }
    int totalLogLines() const { return totalLogLines_; }

public slots:
    // --- QML 可调用槽 ---

    /// 扫描插件目录获取 workflow 列表
    void scanWorkflows();

    /// 登录：默认账户 admin/admin、engineer/engineer、operator/operator
    Q_INVOKABLE bool login(const QString& userName, const QString& password);
    Q_INVOKABLE void logout();

    /// 将编辑器中的 workflow JSON 写入临时文件，并设为当前选中 workflow
    Q_INVOKABLE QString stageWorkflowForRun(const QString& jsonText, const QString& workflowId);

    /// 保存当前 workflow 为 .xlsx 文件到 Workflows/ 目录
    Q_INVOKABLE QString saveCurrentWorkflow(const QString& jsonText, const QString& workflowId);

    /// 启动 Orchestrator 执行选中的 workflow
    void runSelected();

    /// 重试失败的 workflow
    void retryFailed();

    /// 从 state DB 恢复
    void resumeFromState();

    /// 停止当前执行
    void stop();

    /// 浏览插件目录
    void browsePluginDirectory(const QString& currentDir);
    void browseDirectory(const QString& currentDir, const QString& purpose);

    /// 浏览 state 文件
    void browseStateFile(const QString& currentFile);

    /// 刷新硬件状态
    void refreshHardwareStatus();

    /// 清除日志
    void clearLog();

    /// 暂停执行（发送暂停指令到工作流引擎）
    Q_INVOKABLE void pauseExecution();

    /// 恢复执行
    Q_INVOKABLE void resumeExecution();

    /// 跳过当前步骤
    Q_INVOKABLE void skipStep();

    /// 浏览用例文件（.xlsx / .json），返回选择的路径
    Q_INVOKABLE QString browseForTestCase();

    // --- P1 并行看板槽 ---
    /// 刷新仪表盘数据
    Q_INVOKABLE void refreshDashboard();

    /// 获取指定 CELL 的健康状态
    Q_INVOKABLE QString cellHealth(int cellId);

    /// 获取指定 CELL 的已完成步骤数
    Q_INVOKABLE int cellCompletedSteps(int cellId);

    /// 获取指定 CELL 的报告目录
    Q_INVOKABLE QString cellReportsDir(int cellId);

    // --- 每CELL独立操作槽 (CellPanel 调用) ---
    /// 设置 CELL 的 SN
    Q_INVOKABLE void setCellSerial(int cellId, const QString& sn);
    /// 设置 CELL 的测试脚本路径
    Q_INVOKABLE void setCellScript(int cellId, const QString& path);
    /// 启动指定 CELL 执行
    Q_INVOKABLE void runCell(int cellId);
    /// 停止指定 CELL 执行
    Q_INVOKABLE void stopCell(int cellId);
    /// 暂停指定 CELL
    Q_INVOKABLE void pauseCell(int cellId);
    /// 恢复指定 CELL
    Q_INVOKABLE void resumeCell(int cellId);
    /// 获取 CELL 的调试日志
    Q_INVOKABLE QString cellDebugLog(int cellId) const;
    /// 获取 CELL 运行状态 (running/stopped/completed/failed)
    Q_INVOKABLE QString cellRunStatus(int cellId) const;
    /// 获取 CELL 的状态文本
    Q_INVOKABLE QString cellStatusText(int cellId) const;    /// 获取指定 CELL 关联的任务数量
    Q_INVOKABLE int cellTaskCount(int cellId) const;
    /// 检查指定 CELL 是否有失败任务
    Q_INVOKABLE bool cellHasFailed(int cellId) const;
    /// 检查指定 CELL 是否有运行中任务
    Q_INVOKABLE bool cellHasRunning(int cellId) const;
    /// 检查指定 CELL 的所有任务是否都已完成
    Q_INVOKABLE bool cellAllDone(int cellId) const;
signals:
    void pluginDirectoryChanged();
    void workflowDirectoryChanged();
    void reportDirectoryChanged();
    void stateFilePathChanged();
    void cellCountChanged();
    void stopOnFailureChanged();
    void runningChanged();
    void statusTextChanged();
    void workflowPathsChanged();
    void selectedWorkflowsChanged();
    void logTextChanged();
    void telemetryUpdated();
    void dashboardUpdated();

    /// CELL 状态变化信号 (cellId, isRunning, statusText)
    void cellStateChanged(int cellId, bool running, const QString& statusText);
    /// CELL SN 变化信号
    void cellSerialChanged(int cellId, const QString& sn);
    /// CELL 配置列表变化
    void cellConfigsChanged();
    void authenticationChanged();

private slots:
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void appendLog(const QString& line);
    void parseTelemetryLine(const QString& line);
    void updateTaskFromTelemetry(const QJsonObject& obj);
    void startOrchestration(bool resume, const QStringList& workflows);
    QStringList buildOrchestratorArgs(bool resume, const QStringList& workflows) const;

    // 配置
    QString pluginDirectory_;
    QString workflowDirectory_;
    QString reportDirectory_;
    QString stateFilePath_;
    int cellCount_ = 1;
    QVariantList cellIds_;       // [0, 1, 2, ...] for QML Repeater
    bool stopOnFailure_ = false;
    QStringList workflowPaths_;
    QStringList selectedWorkflows_;
    bool authenticated_ = false;
    QString userName_;
    QString userRole_;

    // 每CELL配置与运行时状态
    struct CellRunState {
        bool running = false;
        QString statusText = "Idle";
        QString serialNumber;
        QString scriptPath;
        QProcess* process = nullptr;
        QString pendingOutput;
        QString debugLog;
    };
    QList<CellRunState> cellStates_;
    CellRunState& cellState(int cellId);
    const CellRunState& cellState(int cellId) const;
    void initCellStates();
    QVariantList cellConfigs() const;
    void emitCellConfigsChanged();

    // 运行时
    QProcess* process_ = nullptr;  // 兼容旧版统一进程模式
    QString statusText_ = "Idle";
    QString pendingOutput_;
    QString logText_;
    QString logHtml_;
    int totalLogLines_ = 0;

    // 模型
    TaskListModel taskListModel_;
    ResourceHoldingModel resourceHoldingModel_;
    CapabilityListModel capabilityListModel_;
    TelemetrySummary telemetry_;
    double passRate_ = 0.0;

    // 仪表盘 getter
    int healthyCellCount() const;
    int runningTaskCount() const;
    int failedTaskCount() const;
    int resourceConflictCount() const;
    QObject* resourceHoldingModel() { return &resourceHoldingModel_; }
    QObject* capabilityRegistryModel() { return &capabilityListModel_; }

    // Dashboard 去重计数（避免 task/workflow 重试导致重复累计）
    QSet<int> uniqueTasksStarted_;
    QSet<int> uniqueTasksFinished_;
    QSet<QString> uniqueWorkflowsStarted_;
    QSet<QString> uniqueWorkflowsFinished_;
    QSet<QString> uniqueWorkflowsFailed_;
    QHash<QString, QString> workflowOutcomeInRun_; // key: workflowId/path, value: none/failed/finished
};

} // namespace eon::studio
