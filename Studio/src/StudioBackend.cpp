#include "eon/studio/StudioBackend.h"

#include "eon/domain/WorkflowDefinition.h"
#include "eon/infra/XlsxParser.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStandardPaths>

// QFileDialog 需要 Widgets，QML 模式下用平台原生对话框
// 此处预留接口，实际调用由 QML FileDialog 组件完成

#include <QFileDialog>

namespace eon::studio {

/// 执行控制文件路径（Studio ↔ RuntimeWorker 间通过临时文件通信）
static QString controlFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + "/eonstudio-exec-control.json";
}

static void writeControlFile(const QString& command) {
    QJsonObject obj;
    obj["command"] = command;
    obj["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QFile file(controlFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        file.close();
    }
}

// ============================================================================
// TaskListModel
// ============================================================================

TaskListModel::TaskListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int TaskListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : tasks_.size();
}

QVariant TaskListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= tasks_.size()) return {};

    const auto& t = tasks_.at(index.row());
    switch (role) {
    case TaskIdRole:       return t.taskId;
    case CellIdRole:       return t.cellId;
    case WorkflowIdRole:   return t.workflowId;
    case WorkflowPathRole: return t.workflowPath;
    case StatusRole:       return t.status;
    case AttemptRole:      return t.attempt;
    case MaxAttemptsRole:  return t.maxAttempts;
    case PriorityRole:     return t.priority;
    case ExitCodeRole:     return t.exitCode;
    case LastErrorRole:    return t.lastError;
    case MeasurementNameRole: return t.measurementName;
    case MeasuredValueRole:   return t.measuredValue;
    case MeasuredUnitRole:    return t.measuredUnit;
    case ResultItemsRole:     return t.resultItems;
    case LowerLimitRole:      return t.lowerLimit;
    case UpperLimitRole:      return t.upperLimit;
    case ResultTextRole:      return t.resultText;
    case AnalyzeMessageRole:  return t.analyzeMessage;
    case ElapsedMsRole:       return t.elapsedMs;
    default: break;
    }
    return {};
}

QHash<int, QByteArray> TaskListModel::roleNames() const {
    return {
        {TaskIdRole, "taskId"},
        {CellIdRole, "cellId"},
        {WorkflowIdRole, "workflowId"},
        {WorkflowPathRole, "workflowPath"},
        {StatusRole, "status"},
        {AttemptRole, "attempt"},
        {MaxAttemptsRole, "maxAttempts"},
        {PriorityRole, "priority"},
        {ExitCodeRole, "exitCode"},
        {LastErrorRole, "lastError"},
        {MeasurementNameRole, "measurementName"},
        {MeasuredValueRole, "measuredValue"},
        {MeasuredUnitRole, "measuredUnit"},
        {ResultItemsRole, "resultItems"},
        {LowerLimitRole, "lowerLimit"},
        {UpperLimitRole, "upperLimit"},
        {ResultTextRole, "resultText"},
        {AnalyzeMessageRole, "analyzeMessage"},
        {ElapsedMsRole, "elapsedMs"}
    };
}

void TaskListModel::updateTask(const TaskInfo& task) {
    for (int i = 0; i < tasks_.size(); ++i) {
        if (tasks_[i].taskId == task.taskId) {
            tasks_[i] = task;
            taskMap_[task.taskId] = &tasks_[i];
            emit dataChanged(index(i), index(i));
            return;
        }
    }
    // 新任务
    beginInsertRows({}, tasks_.size(), tasks_.size());
    tasks_.append(task);
    taskMap_[task.taskId] = &tasks_.last();
    endInsertRows();
}

void TaskListModel::clearCellTasks(int cellId) {
    beginResetModel();
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
        [cellId](const TaskInfo& t) { return t.cellId == cellId; }),
        tasks_.end());
    taskMap_.clear();
    for (auto& t : tasks_) {
        taskMap_[t.taskId] = &t;
    }
    endResetModel();
}

void TaskListModel::clear() {
    beginResetModel();
    tasks_.clear();
    taskMap_.clear();
    endResetModel();
}

// ============================================================================
// ResourceHoldingModel
// ============================================================================

ResourceHoldingModel::ResourceHoldingModel(QObject* parent)
    : QAbstractListModel(parent) {}

int ResourceHoldingModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : holdings_.size();
}

QVariant ResourceHoldingModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= holdings_.size()) return {};
    const auto& h = holdings_[index.row()];
    switch (role) {
    case ResourceIdRole: return h["resourceId"].toString();
    case OwnerCellRole:  return h["ownerCell"].toInt();
    case ModeRole:       return h["mode"].toString();
    case ConflictRole:   return h["conflict"].toBool();
    }
    return {};
}

QHash<int, QByteArray> ResourceHoldingModel::roleNames() const {
    return {{ResourceIdRole, "resourceId"}, {OwnerCellRole, "ownerCell"},
            {ModeRole, "mode"}, {ConflictRole, "conflict"}};
}

void ResourceHoldingModel::update(const QList<QJsonObject>& holdings) {
    beginResetModel();
    holdings_ = holdings;
    endResetModel();
}

void ResourceHoldingModel::clear() {
    beginResetModel();
    holdings_.clear();
    endResetModel();
}

// ============================================================================
// CapabilityListModel
// ============================================================================

CapabilityListModel::CapabilityListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int CapabilityListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : plugins_.size();
}

QVariant CapabilityListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= plugins_.size()) return {};
    const auto& p = plugins_[index.row()];
    switch (role) {
    case PluginIdRole:     return p["pluginId"].toString();
    case VersionRole:      return p["version"].toString();
    case CapabilitiesRole: return p["capabilities"].toString();
    }
    return {};
}

QHash<int, QByteArray> CapabilityListModel::roleNames() const {
    return {{PluginIdRole, "pluginId"}, {VersionRole, "version"},
            {CapabilitiesRole, "capabilities"}};
}

void CapabilityListModel::update(const QList<QJsonObject>& plugins) {
    beginResetModel();
    plugins_ = plugins;
    endResetModel();
}

void CapabilityListModel::clear() {
    beginResetModel();
    plugins_.clear();
    endResetModel();
}

// ============================================================================
// 仪表盘辅助方法
// ============================================================================

int StudioBackend::healthyCellCount() const {
    // 简化：假设所有 CELL 健康
    return cellCount_;
}

int StudioBackend::runningTaskCount() const {
    int count = 0;
    for (int i = 0; i < taskListModel_.rowCount(); ++i) {
        auto idx = taskListModel_.index(i, 0);
        if (idx.data(TaskListModel::StatusRole).toString() == "running")
            count++;
    }
    return count;
}

int StudioBackend::failedTaskCount() const {
    int count = 0;
    for (int i = 0; i < taskListModel_.rowCount(); ++i) {
        auto idx = taskListModel_.index(i, 0);
        if (idx.data(TaskListModel::StatusRole).toString() == "failed")
            count++;
    }
    return count;
}

int StudioBackend::resourceConflictCount() const {
    return resourceHoldingModel_.rowCount();
}

void StudioBackend::refreshDashboard() {
    // 模拟刷新资源持有信息
    QList<QJsonObject> holdings;
    for (int c = 0; c < cellCount_ && c < 4; ++c) {
        QJsonObject h;
        h["resourceId"] = QString("VISA::GPIB::%1").arg(c + 1);
        h["ownerCell"] = c;
        h["mode"] = "exclusive";
        h["conflict"] = false;
        holdings.append(h);
    }
    resourceHoldingModel_.update(holdings);

    // 模拟刷新能力列表
    QList<QJsonObject> plugins;
    {
        QJsonObject p;
        p["pluginId"] = "ScpiStep";
        p["version"] = "0.1";
        p["capabilities"] = "scpi, visa";
        plugins.append(p);
    }
    {
        QJsonObject p;
        p["pluginId"] = "PowerSupply";
        p["version"] = "0.1";
        p["capabilities"] = "power, scpi";
        plugins.append(p);
    }
    {
        QJsonObject p;
        p["pluginId"] = "Multimeter";
        p["version"] = "0.1";
        p["capabilities"] = "measure, scpi";
        plugins.append(p);
    }
    capabilityListModel_.update(plugins);

    emit dashboardUpdated();
}

QString StudioBackend::cellHealth(int cellId) {
    return (cellId < cellCount_) ? "running" : "idle";
}

int StudioBackend::cellCompletedSteps(int cellId) {
    int count = 0;
    for (int i = 0; i < taskListModel_.rowCount(); ++i) {
        auto idx = taskListModel_.index(i, 0);
        if (idx.data(TaskListModel::CellIdRole).toInt() == cellId)
            count++;
    }
    return count;
}

QString StudioBackend::cellReportsDir(int cellId) {
    return QDir(reportDirectory_).filePath(QString("cell-%1").arg(cellId, 2, 10, QChar('0')));
}

// ============================================================================
// StudioBackend
// ============================================================================

StudioBackend::StudioBackend(QObject* parent)
    : QObject(parent)
{
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(process_, &QProcess::readyReadStandardOutput,
            this, &StudioBackend::onProcessOutput);
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &StudioBackend::onProcessFinished);

    // 默认路径
    pluginDirectory_ = QDir(QCoreApplication::applicationDirPath()).filePath("../Plugins");
    workflowDirectory_ = QDir(QCoreApplication::applicationDirPath()).filePath("../Workflows");
    reportDirectory_ = QDir(QCoreApplication::applicationDirPath()).filePath("../reports");
    stateFilePath_  = QDir(QCoreApplication::applicationDirPath()).filePath("studio-state.db");
    cellIds_.append(0);  // 默认 1 个 CELL
    initCellStates();

    scanWorkflows();
}

StudioBackend::~StudioBackend() {
    // 停止所有 CELL 进程
    for (auto& state : cellStates_) {
        if (state.process && state.process->state() != QProcess::NotRunning) {
            state.process->terminate();
            state.process->waitForFinished(2000);
            delete state.process;
            state.process = nullptr;
        }
    }
    // 兼容旧版统一进程
    if (process_->state() != QProcess::NotRunning) {
        process_->terminate();
        process_->waitForFinished(2000);
    }
}

// ============================================================================
// 属性访问器
// ============================================================================

QString StudioBackend::pluginDirectory() const    { return pluginDirectory_; }
QString StudioBackend::workflowDirectory() const  { return workflowDirectory_; }
QString StudioBackend::reportDirectory() const    { return reportDirectory_; }
QString StudioBackend::stateFilePath() const       { return stateFilePath_; }
int StudioBackend::cellCount() const               { return cellCount_; }
QVariantList StudioBackend::cellIds() const         { return cellIds_; }
bool StudioBackend::authenticated() const          { return authenticated_; }
QString StudioBackend::userName() const             { return userName_; }
QString StudioBackend::userRole() const             { return userRole_; }
bool StudioBackend::canConfigure() const            { return authenticated_ && userRole_ != "operator"; }
bool StudioBackend::stopOnFailure() const          { return stopOnFailure_; }
bool StudioBackend::isRunning() const {
    if (process_ && process_->state() != QProcess::NotRunning) return true;
    for (const auto& state : cellStates_) {
        if (state.running || (state.process && state.process->state() != QProcess::NotRunning))
            return true;
    }
    return false;
}
QString StudioBackend::statusText() const          { return statusText_; }
QStringList StudioBackend::workflowPaths() const   { return workflowPaths_; }
QStringList StudioBackend::selectedWorkflows() const { return selectedWorkflows_; }
QString StudioBackend::logText() const             { return logText_; }

void StudioBackend::setPluginDirectory(const QString& dir) {
    if (!canConfigure()) return;
    if (pluginDirectory_ != dir) {
        pluginDirectory_ = dir;
        emit pluginDirectoryChanged();
        scanWorkflows();
    }
}

void StudioBackend::setWorkflowDirectory(const QString& dir) {
    if (!canConfigure()) return;
    if (workflowDirectory_ != dir) {
        workflowDirectory_ = dir;
        emit workflowDirectoryChanged();
        scanWorkflows();
    }
}

void StudioBackend::setReportDirectory(const QString& dir) {
    if (!canConfigure()) return;
    if (reportDirectory_ != dir) {
        reportDirectory_ = dir;
        emit reportDirectoryChanged();
        emit cellConfigsChanged();
    }
}

void StudioBackend::setStateFilePath(const QString& path) {
    if (!canConfigure()) return;
    if (stateFilePath_ != path) {
        stateFilePath_ = path;
        emit stateFilePathChanged();
    }
}

void StudioBackend::setCellCount(int count) {
    if (!canConfigure()) return;
    count = qBound(1, count, 16);
    if (cellCount_ != count) {
        cellCount_ = count;
        cellIds_.clear();
        for (int i = 0; i < count; ++i)
            cellIds_.append(i);
        initCellStates();
        emit cellCountChanged();
        emitCellConfigsChanged();
    }
}

bool StudioBackend::login(const QString& userName, const QString& password) {
    struct Account { const char* name; const char* secret; const char* role; };
    static const Account accounts[] = {
        {"admin", "admin", "admin"},
        {"engineer", "engineer", "engineer"},
        {"operator", "operator", "operator"}
    };
    for (const auto& account : accounts) {
        if (userName.trimmed() == QLatin1String(account.name) &&
            password == QLatin1String(account.secret)) {
            authenticated_ = true;
            userName_ = userName.trimmed();
            userRole_ = QLatin1String(account.role);
            emit authenticationChanged();
            return true;
        }
    }
    return false;
}

void StudioBackend::logout() {
    if (!authenticated_) return;
    authenticated_ = false;
    userName_.clear();
    userRole_.clear();
    emit authenticationChanged();
}

void StudioBackend::initCellStates() {
    cellStates_.clear();
    for (int i = 0; i < cellCount_; ++i) {
        CellRunState state;
        state.statusText = "Idle";
        cellStates_.append(state);
    }
}

StudioBackend::CellRunState& StudioBackend::cellState(int cellId) {
    if (cellId < 0 || cellId >= cellStates_.size()) {
        static CellRunState dummy;
        return dummy;
    }
    return cellStates_[cellId];
}

const StudioBackend::CellRunState& StudioBackend::cellState(int cellId) const {
    if (cellId < 0 || cellId >= cellStates_.size()) {
        static const CellRunState dummy;
        return dummy;
    }
    return cellStates_[cellId];
}

QVariantList StudioBackend::cellConfigs() const {
    QVariantList list;
    for (int i = 0; i < cellStates_.size(); ++i) {
        QVariantMap map;
        map["cellId"] = i;
        map["serialNumber"] = cellStates_[i].serialNumber;
        map["scriptPath"] = cellStates_[i].scriptPath;
        map["running"] = cellStates_[i].running;
        map["statusText"] = cellStates_[i].statusText;
        list.append(map);
    }
    return list;
}

void StudioBackend::emitCellConfigsChanged() {
    emit cellConfigsChanged();
}

// ============================================================================
// 每CELL独立操作
// ============================================================================

void StudioBackend::setCellSerial(int cellId, const QString& sn) {
    if (!canConfigure()) return;
    auto& state = cellState(cellId);
    if (state.serialNumber != sn) {
        state.serialNumber = sn;
        emit cellSerialChanged(cellId, sn);
        emitCellConfigsChanged();
    }
}

void StudioBackend::setCellScript(int cellId, const QString& path) {
    if (!canConfigure()) return;
    auto& state = cellState(cellId);
    if (state.scriptPath != path) {
        state.scriptPath = path;
        emitCellConfigsChanged();
    }
}

void StudioBackend::runCell(int cellId) {
    auto& state = cellState(cellId);
    if (state.running) return;

    // 清除该 CELL 上一轮的测试结果
    taskListModel_.clearCellTasks(cellId);

    const QString scriptPath = state.scriptPath;
    if (scriptPath.isEmpty()) {
        state.statusText = "No test case selected!";
        emit cellStateChanged(cellId, false, state.statusText);
        return;
    }

    // 构建 Orchestrator 启动参数：为该 CELL 运行指定的 workflow
    const QString orchestratorName =
#if defined(Q_OS_WIN)
        "eon-orchestrator.exe";
#else
        "eon-orchestrator";
#endif
    const QString orchestratorPath = QDir(QCoreApplication::applicationDirPath()).filePath(orchestratorName);
    if (!QFileInfo::exists(orchestratorPath)) {
        appendLog(QString("[CELL:%1 ERROR] Orchestrator not found: %2").arg(cellId).arg(orchestratorPath));
        state.statusText = "Orchestrator not found";
        emit cellStateChanged(cellId, false, state.statusText);
        return;
    }

    // 清理旧进程
    if (state.process) {
        state.process->terminate();
        state.process->waitForFinished(2000);
        delete state.process;
        state.process = nullptr;
    }

    // 创建独立 QProcess
    state.process = new QProcess(this);
    state.process->setProcessChannelMode(QProcess::MergedChannels);
    state.pendingOutput.clear();
    state.debugLog.clear();

    // 连接输出信号（带 cellId 捕获）
    const int capturedCellId = cellId;
    connect(state.process, &QProcess::readyReadStandardOutput, this, [this, capturedCellId]() {
        auto& st = cellState(capturedCellId);
        QByteArray data = st.process ? st.process->readAllStandardOutput() : QByteArray();
        st.pendingOutput += QString::fromLocal8Bit(data);

        while (true) {
            const int pos = st.pendingOutput.indexOf('\n');
            if (pos < 0) break;
            const QString line = st.pendingOutput.left(pos).trimmed();
            st.pendingOutput.remove(0, pos + 1);
            if (!line.isEmpty()) {
                appendLog(QString("[CELL:%1] %2").arg(capturedCellId).arg(line));
                st.debugLog += line + "\n";
                parseTelemetryLine(line);
            }
        }
    });

    connect(state.process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, capturedCellId](int exitCode, QProcess::ExitStatus exitStatus) {
        auto& st = cellState(capturedCellId);
        if (!st.pendingOutput.trimmed().isEmpty()) {
            appendLog(QString("[CELL:%1] %2").arg(capturedCellId).arg(st.pendingOutput.trimmed()));
            st.debugLog += st.pendingOutput.trimmed() + "\n";
            st.pendingOutput.clear();
        }
        st.running = false;
        const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);
        st.statusText = ok ? "Completed ✓" : QString("Failed (exit %1)").arg(exitCode);
        emit cellStateChanged(capturedCellId, false, st.statusText);
        emitCellConfigsChanged();
    });

    // 启动
    QStringList args;
    args << "--cells" << "1";  // 每个 CELL 独立进程
    if (stopOnFailure_) args << "--stop-on-failure";
    args << pluginDirectory_ << scriptPath;

    appendLog(QString("[CELL:%1 DEBUG] Starting: %2 %3")
              .arg(cellId).arg(orchestratorPath, args.join(' ')));
    state.debugLog += QString("[START] %1 %2\n").arg(orchestratorPath, args.join(' '));

    state.process->setProgram(orchestratorPath);
    state.process->setArguments(args);
    state.process->start();

    if (!state.process->waitForStarted(5000)) {
        appendLog(QString("[CELL:%1 ERROR] Failed to start orchestrator.").arg(cellId));
        state.statusText = "Start failed";
        state.running = false;
        emit cellStateChanged(cellId, false, state.statusText);
        emitCellConfigsChanged();
        return;
    }

    state.running = true;
    state.statusText = "Running...";
    emit cellStateChanged(cellId, true, state.statusText);
    emitCellConfigsChanged();
}

void StudioBackend::stopCell(int cellId) {
    auto& state = cellState(cellId);
    if (!state.running && !state.process) {
        // 即使进程已结束也确保状态显示 Aborted
        state.statusText = "Aborted";
        emit cellStateChanged(cellId, false, state.statusText);
        emitCellConfigsChanged();
        return;
    }

    // 标记为已停止（UI 立刻响应，不阻塞）
    state.running = false;
    state.statusText = "Aborted";

    // 发送 abort 指令给 orchestrator
    writeControlFile("abort");

    if (state.process) {
        if (state.process->state() != QProcess::NotRunning) {
            state.process->kill();  // Windows TerminateProcess，瞬间生效
            state.process->waitForFinished(500);  // 极短等待，几乎不阻塞
        }
        state.process->deleteLater();
        state.process = nullptr;
    }

    emit cellStateChanged(cellId, false, state.statusText);
    emitCellConfigsChanged();
}

void StudioBackend::pauseCell(int cellId) {
    auto& state = cellState(cellId);
    if (!state.running) return;
    writeControlFile("pause");
    state.statusText = "Paused";
    emit cellStateChanged(cellId, true, state.statusText);
}

void StudioBackend::resumeCell(int cellId) {
    auto& state = cellState(cellId);
    if (!state.running) return;
    writeControlFile("resume");
    state.statusText = "Running...";
    emit cellStateChanged(cellId, true, state.statusText);
}

QString StudioBackend::cellDebugLog(int cellId) const {
    const auto& state = cellState(cellId);
    if (state.debugLog.isEmpty()) return "(no debug output yet)";
    return state.debugLog;
}

QString StudioBackend::cellRunStatus(int cellId) const {
    const auto& state = cellState(cellId);
    if (state.running) return "running";
    if (state.statusText.contains("Failed")) return "failed";
    if (state.statusText.contains("Completed")) return "completed";
    return "stopped";
}

QString StudioBackend::cellStatusText(int cellId) const {
    return cellState(cellId).statusText;
}

int StudioBackend::cellTaskCount(int cellId) const {
    int count = 0;
    for (int i = 0; i < taskListModel_.rowCount(); ++i) {
        const auto idx = taskListModel_.index(i);
        if (idx.data(TaskListModel::CellIdRole).toInt() == cellId)
            count++;
    }
    return count;
}

bool StudioBackend::cellHasFailed(int cellId) const {
    for (int i = 0; i < taskListModel_.rowCount(); ++i) {
        const auto idx = taskListModel_.index(i);
        if (idx.data(TaskListModel::CellIdRole).toInt() == cellId) {
            const QString status = idx.data(TaskListModel::StatusRole).toString();
            if (status == "failed") return true;
        }
    }
    return false;
}

bool StudioBackend::cellHasRunning(int cellId) const {
    for (int i = 0; i < taskListModel_.rowCount(); ++i) {
        const auto idx = taskListModel_.index(i);
        if (idx.data(TaskListModel::CellIdRole).toInt() == cellId) {
            const QString status = idx.data(TaskListModel::StatusRole).toString();
            if (status == "running") return true;
        }
    }
    return false;
}

bool StudioBackend::cellAllDone(int cellId) const {
    bool hasAny = false;
    for (int i = 0; i < taskListModel_.rowCount(); ++i) {
        const auto idx = taskListModel_.index(i);
        if (idx.data(TaskListModel::CellIdRole).toInt() == cellId) {
            hasAny = true;
            const QString status = idx.data(TaskListModel::StatusRole).toString();
            if (status != "succeeded" && status != "failed" && status != "skipped")
                return false;
        }
    }
    return hasAny;  // 如果没有任务，不算"全部完成"
}

void StudioBackend::setStopOnFailure(bool stop) {
    if (!canConfigure()) return;
    if (stopOnFailure_ != stop) {
        stopOnFailure_ = stop;
        emit stopOnFailureChanged();
    }
}

void StudioBackend::setSelectedWorkflows(const QStringList& paths) {
    if (!canConfigure()) return;
    selectedWorkflows_ = paths;
    emit selectedWorkflowsChanged();
}

// ============================================================================
// 操作
// ============================================================================

void StudioBackend::scanWorkflows() {
    workflowPaths_.clear();

    // 运行时 JSON workflows（开发/调试用）
    const QStringList scanDirs = {
        workflowDirectory_,
        QDir(QCoreApplication::applicationDirPath()).filePath("../../Workflows")
    };

    for (const auto& dirPath : scanDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        // 同时扫描 .json（Workflow）和 .xlsx（Excel 测试用例）
        const auto files = dir.entryInfoList({"*.json", "*.xlsx"}, QDir::Files, QDir::Name);
        for (const auto& fi : files) {
            if (!workflowPaths_.contains(fi.absoluteFilePath()))
                workflowPaths_.append(fi.absoluteFilePath());
        }
    }

    emit workflowPathsChanged();
}

QString StudioBackend::stageWorkflowForRun(const QString& jsonText, const QString& workflowId) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        statusText_ = "Invalid workflow JSON";
        emit statusTextChanged();
        return {};
    }

    QString baseName = workflowId.trimmed();
    if (baseName.isEmpty()) {
        baseName = doc.object().value("workflowId").toString("editor-workflow");
    }
    baseName.replace(QRegularExpression(R"([^A-Za-z0-9_.-]+)"), "_");
    if (!baseName.endsWith(".workflow.json")) {
        baseName += ".workflow.json";
    }

    const QString tempDirPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir tempDir(tempDirPath);
    if (!tempDir.exists()) {
        tempDir.mkpath(".");
    }

    const QString fullPath = tempDir.filePath(QString("eonstudio-%1").arg(baseName));
    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        statusText_ = QString("Cannot write workflow file: %1").arg(fullPath);
        emit statusTextChanged();
        return {};
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    setSelectedWorkflows({fullPath});
    if (!workflowPaths_.contains(fullPath)) {
        workflowPaths_.prepend(fullPath);
        emit workflowPathsChanged();
    }

    statusText_ = QString("Workflow staged: %1").arg(QFileInfo(fullPath).fileName());
    emit statusTextChanged();
    return fullPath;
}

QString StudioBackend::saveCurrentWorkflow(const QString& jsonText, const QString& workflowId) {
    // 解析 JSON → WorkflowDefinition
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        statusText_ = "Save failed: invalid JSON";
        emit statusTextChanged();
        return {};
    }

    // 将 JSON 转换为 WorkflowDefinition
    eon::domain::WorkflowDefinition wf;
    wf.workflowId = workflowId;
    const QJsonObject root = doc.object();
    wf.entryStepId = root.value("entryStepId").toString();
    const QJsonArray steps = root.value("steps").toArray();
    for (const auto& sVal : steps) {
        const QJsonObject so = sVal.toObject();
        eon::domain::ActivityStep step;
        step.stepId = so.value("stepId").toString();
        step.pluginId = so.value("pluginId").toString();
        step.parallelGroupId = so.value("parallelGroupId").toString();
        step.policy.maxRetries = so.value("maxRetries").toInt(0);
        step.policy.timeoutMs = so.value("timeoutMs").toInt(0);
        step.policy.failurePolicy = (so.value("failurePolicy").toString("fail_fast") == "continue_on_error")
            ? eon::domain::FailurePolicy::ContinueOnError
            : eon::domain::FailurePolicy::FailFast;
        step.conditionKey = so.value("conditionKey").toString();
        step.conditionEquals = so.value("conditionEquals").toString();
        step.compensationStepId = so.value("compensationStepId").toString();
        step.onSuccessStepId = so.value("onSuccessStepId").toString();
        step.onFailureStepId = so.value("onFailureStepId").toString();
        step.onSkippedStepId = so.value("onSkippedStepId").toString();
        wf.steps.append(step);
    }

    // 确定保存路径 → Workflows/ 目录（与可执行文件同级）
    QString baseName = workflowId.trimmed();
    if (baseName.isEmpty()) baseName = "new-workflow";
    baseName.replace(QRegularExpression(R"([^A-Za-z0-9_.-]+)"), "_");

    const QString workflowsDir = QCoreApplication::applicationDirPath() + "/Workflows";
    QDir().mkpath(workflowsDir);

    QString xlsxPath = workflowsDir + "/" + baseName;
    if (!xlsxPath.endsWith(".xlsx", Qt::CaseInsensitive))
        xlsxPath += ".xlsx";

    QString error;
    if (!eon::infra::writeWorkflowDefinitionXlsx(xlsxPath, wf, &error)) {
        statusText_ = QString("Save failed: %1").arg(error);
        emit statusTextChanged();
        return {};
    }

    // 同时保留 JSON staging 以兼容 Run
    stageWorkflowForRun(jsonText, workflowId);

    statusText_ = QString("Saved: %1").arg(QFileInfo(xlsxPath).fileName());
    emit statusTextChanged();
    return xlsxPath;
}

QString StudioBackend::browseForTestCase() {
    const QString path = QFileDialog::getOpenFileName(
        nullptr,
        "Open Test Case",
        QString(),
        "Test Case Files (*.xlsx *.json);;Excel Files (*.xlsx);;JSON Workflow (*.json)"
    );
    return path;
}

void StudioBackend::runSelected() {
    if (isRunning()) return;
    if (selectedWorkflows_.isEmpty()) {
        statusText_ = "No workflows selected";
        emit statusTextChanged();
        return;
    }
    startOrchestration(false, selectedWorkflows_);
}

void StudioBackend::retryFailed() {
    if (isRunning()) return;

    QStringList failedPaths;
    for (int i = 0; i < taskListModel_.rowCount(); ++i) {
        const auto idx = taskListModel_.index(i);
        const QString status = taskListModel_.data(idx, TaskListModel::StatusRole).toString();
        if (status == "failed" || status == "skipped") {
            const QString path = taskListModel_.data(idx, TaskListModel::WorkflowPathRole).toString();
            if (!path.isEmpty()) failedPaths.append(path);
        }
    }
    failedPaths.removeDuplicates();

    if (failedPaths.isEmpty()) {
        statusText_ = "No failed workflows to retry";
        emit statusTextChanged();
        return;
    }
    startOrchestration(false, failedPaths);
}

void StudioBackend::resumeFromState() {
    if (isRunning()) return;
    startOrchestration(true, {});
}

void StudioBackend::stop() {
    // 先发送 abort 指令给 RuntimeWorker（如果有运行中的工作流）
    writeControlFile("abort");

    // 停止所有 CELL 进程
    for (int i = 0; i < cellStates_.size(); ++i) {
        stopCell(i);
    }

    // 兼容旧版统一进程
    if (process_ && process_->state() != QProcess::NotRunning) {
        if (!process_->waitForFinished(5000)) {
            process_->terminate();
            if (!process_->waitForFinished(3000)) {
                process_->kill();
                process_->waitForFinished(2000);
            }
        }
    }
}

void StudioBackend::browsePluginDirectory(const QString& currentDir) {
    const QString path = QFileDialog::getExistingDirectory(
        nullptr,
        "Select Plugin Directory",
        currentDir.isEmpty() ? pluginDirectory_ : currentDir
    );
    if (!path.isEmpty()) {
        setPluginDirectory(path);
    }
}

void StudioBackend::browseDirectory(const QString& currentDir, const QString& purpose) {
    if (!canConfigure()) return;
    const QString path = QFileDialog::getExistingDirectory(
        nullptr, purpose, currentDir.isEmpty() ? QDir::homePath() : currentDir);
    if (path.isEmpty()) return;
    if (purpose.contains("Report", Qt::CaseInsensitive)) setReportDirectory(path);
    else setWorkflowDirectory(path);
}

void StudioBackend::browseStateFile(const QString& currentFile) {
    const QString path = QFileDialog::getSaveFileName(
        nullptr,
        "Select State Database File",
        currentFile.isEmpty() ? stateFilePath_ : currentFile,
        "SQLite Database (*.db *.sqlite);;All Files (*)"
    );
    if (!path.isEmpty()) {
        setStateFilePath(path);
    }
}

void StudioBackend::refreshHardwareStatus() {
    // Phase 3 先做占位，Phase 7 接入 HardwareManager
}

void StudioBackend::clearLog() {
    logText_.clear();
    logHtml_.clear();
    totalLogLines_ = 0;
    emit logTextChanged();
}

void StudioBackend::pauseExecution() {
    // 写控制指令到临时文件，RuntimeWorker 的 BreakOffer 回调会检查
    writeControlFile("pause");
    appendLog("{\"event\":\"control.pause\",\"source\":\"studio\"}");
}

void StudioBackend::resumeExecution() {
    writeControlFile("resume");
    appendLog("{\"event\":\"control.resume\",\"source\":\"studio\"}");
}

void StudioBackend::skipStep() {
    writeControlFile("skip");
    appendLog("{\"event\":\"control.skip\",\"source\":\"studio\"}");
}

// ============================================================================
// Orchestrator 管理
// ============================================================================

QStringList StudioBackend::buildOrchestratorArgs(bool resume, const QStringList& workflows) const {
    QStringList args;

    if (cellCount_ > 1) args << "--cells" << QString::number(cellCount_);
    if (!stateFilePath_.trimmed().isEmpty()) args << "--state" << stateFilePath_.trimmed();
    if (stopOnFailure_) args << "--stop-on-failure";

    if (resume) {
        args << "--resume";
        return args;
    }

    args << pluginDirectory_;
    args.append(workflows);
    return args;
}

void StudioBackend::startOrchestration(bool resume, const QStringList& workflows) {
    taskListModel_.clear();
    // Dashboard 计数按用户点击 Run 累积，不在每次运行前清零。
    uniqueTasksStarted_.clear();
    uniqueTasksFinished_.clear();
    uniqueWorkflowsStarted_.clear();
    uniqueWorkflowsFinished_.clear();
    uniqueWorkflowsFailed_.clear();
    workflowOutcomeInRun_.clear();
    emit telemetryUpdated();

    const QString orchestratorName =
#if defined(Q_OS_WIN)
        "eon-orchestrator.exe";
#else
        "eon-orchestrator";
#endif
    const QString orchestratorPath = QDir(QCoreApplication::applicationDirPath()).filePath(orchestratorName);
    if (!QFileInfo::exists(orchestratorPath)) {
        appendLog("[ERROR] Orchestrator not found: " + orchestratorPath);
        statusText_ = "Orchestrator not found";
        emit statusTextChanged();
        return;
    }

    process_->setProgram(orchestratorPath);
    auto args = buildOrchestratorArgs(resume, workflows);
    appendLog(QString("[DEBUG] Starting: %1 %2").arg(orchestratorPath, args.join(' ')));
    process_->setArguments(args);
    process_->start();

    if (!process_->waitForStarted(5000)) {
        appendLog("[ERROR] Failed to start orchestrator.");
        statusText_ = "Start failed";
        emit statusTextChanged();
        return;
    }

    statusText_ = "Running...";
    emit statusTextChanged();
    emit runningChanged();
}

// ============================================================================
// 遥测解析
// ============================================================================

void StudioBackend::onProcessOutput() {
    pendingOutput_ += QString::fromLocal8Bit(process_->readAllStandardOutput());

    while (true) {
        const int pos = pendingOutput_.indexOf('\n');
        if (pos < 0) break;
        const QString line = pendingOutput_.left(pos).trimmed();
        pendingOutput_.remove(0, pos + 1);
        if (!line.isEmpty()) {
            appendLog(line);
            parseTelemetryLine(line);
        }
    }
}

void StudioBackend::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    // 处理剩余数据
    if (!pendingOutput_.trimmed().isEmpty()) {
        appendLog(pendingOutput_.trimmed());
        parseTelemetryLine(pendingOutput_.trimmed());
        pendingOutput_.clear();
    }

    const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);
    statusText_ = ok ? "Completed" : QString("Failed (exit %1)").arg(exitCode);
    emit statusTextChanged();
    emit runningChanged();

    // 计算良率
    const int total = telemetry_.workflowsStarted;
    if (total > 0) {
        passRate_ = static_cast<double>(telemetry_.workflowsFinished) / static_cast<double>(total) * 100.0;
        emit telemetryUpdated();
    }
}

void StudioBackend::appendLog(const QString& line) {
    // 尝试解析 JSON 以格式化显示
    QString displayLine = line;
    QString htmlColor = "#c0c0c0";  // 默认灰色
    QString bgColor;                 // 无背景
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject obj = doc.object();
        const int cellId = obj.value("cellId").toInt(-1);
        const QString event = obj.value("event").toString();
        const QString source = obj.value("source").toString();
        const QString wfId = obj.value("workflowId").toString();
        const QString stepId = obj.value("stepId").toString();
        const QString status = obj.value("status").toString();
        const QString errorStr = obj.value("error").toString();
        const QString dutId = obj.value("dutId").toString();
        const QString doipTx = obj.value("doipTx").toString();
        const QString doipRoutingTx = obj.value("doipRoutingTx").toString();
        const QString doipRx = obj.value("doipRx").toString();

        // 格式化: [CELL:N][事件类型] source: detail
        QString fmt;
        if (cellId >= 0)
            fmt += QString("[CELL:%1]").arg(cellId);
        else
            fmt += "[SYS]";

        // 事件类型图标 + 颜色
        if (event.contains("started")) {
            fmt += " ▶ ";
            htmlColor = "#00bcd4";  // 青色
        } else if (event.contains("finished")) {
            fmt += " ✓ ";
            htmlColor = "#69f0ae";  // 绿色
        } else if (event.contains("failed")) {
            fmt += " ✗ ";
            htmlColor = "#ff5252";  // 红色
        } else if (event.contains("retry")) {
            fmt += " ⟳ ";
            htmlColor = "#ffab00";  // 橙色
        } else if (event.contains("error")) {
            fmt += " ⚠ ";
            htmlColor = "#ff5252";
        } else {
            fmt += " · ";
        }

        fmt += source;
        if (!wfId.isEmpty()) fmt += " " + wfId;
        if (!stepId.isEmpty()) fmt += ":" + stepId;
        if (!dutId.isEmpty()) fmt += " [DUT:" + dutId + "]";
        if (!status.isEmpty() && status != "pending")
            fmt += " → " + status;
        if (!errorStr.isEmpty())
            fmt += " ! " + errorStr;
        if (!doipTx.isEmpty())
            fmt += " TX=" + doipTx;
        if (!doipRoutingTx.isEmpty())
            fmt += " ROUTING_TX=" + doipRoutingTx;
        if (!doipRx.isEmpty())
            fmt += " RX=" + doipRx;

        displayLine = fmt;

        // CELL 背景色
        if (cellId >= 0) {
            static const QStringList cellColors = {
                "#0a1a2a", "#1a0a1a", "#0a1a1a", "#1a1a0a",
                "#1a1010", "#100a1a", "#0a101a", "#1a1a1a"
            };
            bgColor = cellColors.at(cellId % cellColors.size());
        } else {
            bgColor = "#10101a";
        }
    }

    // 构造 HTML 行
    QString htmlLine;
    if (!bgColor.isEmpty()) {
        htmlLine = QString("<p style='background:%1; color:%2; margin:0; padding:1px 2px; font-family:Consolas,\"Courier New\",monospace; font-size:10px'>%3</p>")
                       .arg(bgColor, htmlColor, displayLine.toHtmlEscaped());
    } else {
        htmlLine = QString("<p style='color:%1; margin:0; padding:1px 2px; font-family:Consolas,\"Courier New\",monospace; font-size:10px'>%2</p>")
                       .arg(htmlColor, displayLine.toHtmlEscaped());
    }

    logHtml_ += htmlLine;
    if (logHtml_.length() > 512 * 1024) { // 512KB 上限
        const int cutPos = logHtml_.indexOf("</p>", 2048);
        logHtml_ = cutPos > 0 ? logHtml_.mid(cutPos + 4) : logHtml_.mid(2048);
    }
    totalLogLines_++;
    emit logTextChanged();
}

void StudioBackend::parseTelemetryLine(const QString& line) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonObject obj = doc.object();
    const QString event = obj.value("event").toString();

    auto findTaskRowByWorkflowId = [&](const QString& workflowId) -> int {
        if (workflowId.isEmpty()) return -1;
        for (int row = 0; row < taskListModel_.rowCount(); ++row) {
            const QModelIndex idx = taskListModel_.index(row, 0);
            if (taskListModel_.data(idx, TaskListModel::WorkflowIdRole).toString() == workflowId) {
                return row;
            }
        }
        return -1;
    };

    auto taskFromRow = [&](int row) -> TaskInfo {
        TaskInfo t;
        if (row < 0) return t;
        const QModelIndex idx = taskListModel_.index(row, 0);
        t.taskId = taskListModel_.data(idx, TaskListModel::TaskIdRole).toInt();
        t.cellId = taskListModel_.data(idx, TaskListModel::CellIdRole).toInt();
        t.workflowId = taskListModel_.data(idx, TaskListModel::WorkflowIdRole).toString();
        t.workflowPath = taskListModel_.data(idx, TaskListModel::WorkflowPathRole).toString();
        t.status = taskListModel_.data(idx, TaskListModel::StatusRole).toString();
        t.attempt = taskListModel_.data(idx, TaskListModel::AttemptRole).toInt();
        t.maxAttempts = taskListModel_.data(idx, TaskListModel::MaxAttemptsRole).toInt();
        t.priority = taskListModel_.data(idx, TaskListModel::PriorityRole).toInt();
        t.exitCode = taskListModel_.data(idx, TaskListModel::ExitCodeRole).toInt();
        t.lastError = taskListModel_.data(idx, TaskListModel::LastErrorRole).toString();
        t.measurementName = taskListModel_.data(idx, TaskListModel::MeasurementNameRole).toString();
        t.measuredValue = taskListModel_.data(idx, TaskListModel::MeasuredValueRole).toString();
        t.measuredUnit = taskListModel_.data(idx, TaskListModel::MeasuredUnitRole).toString();
        t.lowerLimit = taskListModel_.data(idx, TaskListModel::LowerLimitRole).toString();
        t.upperLimit = taskListModel_.data(idx, TaskListModel::UpperLimitRole).toString();
        t.resultText = taskListModel_.data(idx, TaskListModel::ResultTextRole).toString();
        t.analyzeMessage = taskListModel_.data(idx, TaskListModel::AnalyzeMessageRole).toString();
        t.elapsedMs = taskListModel_.data(idx, TaskListModel::ElapsedMsRole).toDouble();
        return t;
    };

    // 更新任务状态
    if (event.startsWith("orchestration.task.") || event == "orchestration.scheduling.halted") {
        updateTaskFromTelemetry(obj);
    }

    // 更新任务测量/分析结果（由 runtime worker 透传）
    if (event == "activity.finished" || event == "analyzer.finished" || event == "analyzer.failed") {
        const QString workflowId = obj.value("workflowId").toString();
        const QString stepId = obj.value("stepId").toString();

        // 按 workflowId + stepId 查找或创建步骤行（每个步骤独立显示）
        int row = -1;
        for (int r = 0; r < taskListModel_.rowCount(); ++r) {
            const auto idx = taskListModel_.index(r, 0);
            const QString wf = taskListModel_.data(idx, TaskListModel::WorkflowIdRole).toString();
            const QString wfPath = taskListModel_.data(idx, TaskListModel::WorkflowPathRole).toString();
            // 步骤行以 "step:" 开头标识
            if (wf.startsWith("step:") && wf == ("step:" + workflowId + ":" + stepId)) {
                row = r; break;
            }
        }

        if (row < 0) {
            // 创建新的步骤行
            TaskInfo newTask;
            newTask.taskId = taskListModel_.rowCount() + 1000;
            newTask.cellId = obj.contains("cellId") ? obj.value("cellId").toInt() : 0;
            if (newTask.cellId < 0) newTask.cellId = 0;
            newTask.workflowId = "step:" + workflowId + ":" + stepId;
            newTask.workflowPath = stepId;
            newTask.status = (event == "activity.finished")
                ? (obj.value("resultText").toString() == "FAIL" ? "failed" : "succeeded")
                : "running";
            taskListModel_.updateTask(newTask);
            row = taskListModel_.rowCount() - 1;
        }

        if (row >= 0) {
            TaskInfo task = taskFromRow(row);

            if (event == "activity.finished") {
                const QJsonValue measuredValue = obj.value("measuredValue");
                const QString measuredUnit = obj.value("measuredUnit").toString();
                const QString pluginId = obj.value("pluginId").toString();
                const QString measurementName = obj.value("measurementName").toString();
                task.measurementName = !measurementName.isEmpty() ? measurementName : pluginId;
                if (!measuredValue.isUndefined()) {
                    // DoIP 响应是十六进制字符串，不能调用 toDouble()，否则会显示 0.000。
                    task.measuredValue = measuredValue.isString()
                        ? measuredValue.toString()
                        : QString::number(measuredValue.toDouble(), 'f', 3);
                }
                if (!measuredUnit.isEmpty()) {
                    task.measuredUnit = measuredUnit;
                }
                if (obj.value("resultItems").isArray()) {
                    task.resultItems = obj.value("resultItems").toArray().toVariantList();
                    QStringList summary;
                    for (const auto& item : task.resultItems) {
                        const auto map = item.toMap();
                        const QString name = map.value("name").toString();
                        const QString value = map.value("value").toString();
                        if (!name.isEmpty() && !value.isEmpty())
                            summary << QString("%1=%2").arg(name, value);
                    }
                    if (task.resultItems.size() > 1 && !summary.isEmpty())
                        task.measuredValue = summary.join("; ");
                }
                // 透传限值和判定结果
                if (!obj.value("lowerLimit").isUndefined())
                    task.lowerLimit = obj.value("lowerLimit").toVariant().toString();
                if (!obj.value("upperLimit").isUndefined())
                    task.upperLimit = obj.value("upperLimit").toVariant().toString();
                if (!obj.value("resultText").isUndefined())
                    task.resultText = obj.value("resultText").toString();
                // 设置完成状态和耗时
                task.status = (task.resultText == "FAIL") ? "failed" : "succeeded";
                if (!obj.value("elapsedMs").isUndefined())
                    task.elapsedMs = obj.value("elapsedMs").toDouble();
                // activity.finished 也可能携带 analyzer 结果（当 analyzer 作为步骤执行时）
                const QJsonValue aMin = obj.value("analyzeMin");
                const QJsonValue aMax = obj.value("analyzeMax");
                const QJsonValue aVal = obj.value("analyzeValue");
                const QString aUnit = obj.value("analyzeUnit").toString();
                const QString aMsg = obj.value("analyzeMessage").toString();
                const bool hasPassed = obj.contains("analyzePassed");
                const bool passed = obj.value("analyzePassed").toBool();
                if (!aVal.isUndefined()) {
                    task.measuredValue = QString::number(aVal.toDouble(), 'f', 3);
                }
                if (!aUnit.isEmpty()) {
                    task.measuredUnit = aUnit;
                }
                if (!aMin.isUndefined()) {
                    task.lowerLimit = QString::number(aMin.toDouble(), 'f', 3);
                }
                if (!aMax.isUndefined()) {
                    task.upperLimit = QString::number(aMax.toDouble(), 'f', 3);
                }
                if (!aMsg.isEmpty()) {
                    task.analyzeMessage = aMsg;
                }
                if (hasPassed) {
                    task.resultText = passed ? "PASS" : "FAIL";
                }
            }

            if (event == "analyzer.finished" || event == "analyzer.failed") {
                const QJsonValue vMin = obj.value("analyzeMin");
                const QJsonValue vMax = obj.value("analyzeMax");
                const QJsonValue vVal = obj.value("analyzeValue");
                const QString unit = obj.value("analyzeUnit").toString();
                const QString message = obj.value("analyzeMessage").toString();
                const bool hasPassed = obj.contains("analyzePassed");
                const bool passed = obj.value("analyzePassed").toBool();

                if (!vVal.isUndefined()) {
                    task.measuredValue = QString::number(vVal.toDouble(), 'f', 3);
                }
                if (!unit.isEmpty()) {
                    task.measuredUnit = unit;
                }
                if (!vMin.isUndefined()) {
                    task.lowerLimit = QString::number(vMin.toDouble(), 'f', 3);
                }
                if (!vMax.isUndefined()) {
                    task.upperLimit = QString::number(vMax.toDouble(), 'f', 3);
                }
                if (!message.isEmpty()) {
                    task.analyzeMessage = message;
                }
                if (hasPassed) {
                    task.resultText = passed ? "PASS" : "FAIL";
                }
            }

            taskListModel_.updateTask(task);
        }
    }

    const int taskIdForCount = obj.value("taskId").toInt(0);
    const QString workflowIdForCount = obj.value("workflowId").toString().trimmed();

    // 更新遥测计数（匹配 RuntimeWorker 的事件名）
    if (event == "workflow.started") {
        const QString key = workflowIdForCount.isEmpty() ? obj.value("workflowFilePath").toString() : workflowIdForCount;
        if (!key.isEmpty() && !uniqueWorkflowsStarted_.contains(key)) {
            uniqueWorkflowsStarted_.insert(key);
            telemetry_.workflowsStarted++;
        }
    }
    else if (event == "workflow.finished") {
        const QString key = workflowIdForCount.isEmpty() ? obj.value("workflowFilePath").toString() : workflowIdForCount;
        if (!key.isEmpty()) {
            const QString prev = workflowOutcomeInRun_.value(key, "none");
            if (prev == "none") {
                if (!uniqueWorkflowsFinished_.contains(key)) {
                    uniqueWorkflowsFinished_.insert(key);
                    telemetry_.workflowsFinished++;
                }
            } else if (prev == "failed") {
                if (telemetry_.workflowsFailed > 0) {
                    telemetry_.workflowsFailed--;
                }
                if (!uniqueWorkflowsFinished_.contains(key)) {
                    uniqueWorkflowsFinished_.insert(key);
                }
                telemetry_.workflowsFinished++;
                uniqueWorkflowsFailed_.remove(key);
            }
            workflowOutcomeInRun_.insert(key, "finished");
        }
    }
    else if (event == "workflow.failed") {
        const QString key = workflowIdForCount.isEmpty() ? obj.value("workflowFilePath").toString() : workflowIdForCount;
        if (!key.isEmpty()) {
            const QString prev = workflowOutcomeInRun_.value(key, "none");
            if (prev == "none" && !uniqueWorkflowsFailed_.contains(key)) {
                uniqueWorkflowsFailed_.insert(key);
                telemetry_.workflowsFailed++;
                workflowOutcomeInRun_.insert(key, "failed");
            }
        }
    }
    else if (event == "activity.started")          telemetry_.activitiesStarted++;
    else if (event == "activity.finished")         telemetry_.activitiesFinished++;
    else if (event == "activity.failed")           telemetry_.activitiesFailed++;
    else if (event == "activity.skipped")          telemetry_.activitiesSkipped++;
    else if (event == "activity.retry")            telemetry_.retries++;
    else if (event == "compensation.started")      telemetry_.compensationsStarted++;
    else if (event == "compensation.finished")     telemetry_.compensationsFinished++;
    else if (event == "compensation.failed")       telemetry_.compensationsFailed++;
    else if (event == "analyzer.started")          telemetry_.analyzersStarted++;
    else if (event == "analyzer.finished")         telemetry_.analyzersFinished++;
    else if (event == "reporter.started")          telemetry_.reportersStarted++;
    else if (event == "reporter.finished")         telemetry_.reportersFinished++;
    else if (event == "orchestration.task.started") {
        if (taskIdForCount > 0 && !uniqueTasksStarted_.contains(taskIdForCount)) {
            uniqueTasksStarted_.insert(taskIdForCount);
            telemetry_.tasksStarted++;
        }
    }
    else if (event == "orchestration.task.finished") {
        if (taskIdForCount > 0 && !uniqueTasksFinished_.contains(taskIdForCount)) {
            uniqueTasksFinished_.insert(taskIdForCount);
            telemetry_.tasksFinished++;
        }
    }

    emit telemetryUpdated();
}

void StudioBackend::updateTaskFromTelemetry(const QJsonObject& obj) {
    const int taskId = obj.value("taskId").toInt(0);
    if (taskId <= 0) return;

    TaskInfo task;
    bool found = false;
    for (int row = 0; row < taskListModel_.rowCount(); ++row) {
        const QModelIndex idx = taskListModel_.index(row, 0);
        if (taskListModel_.data(idx, TaskListModel::TaskIdRole).toInt() == taskId) {
            task.taskId = taskId;
            task.cellId = taskListModel_.data(idx, TaskListModel::CellIdRole).toInt();
            task.workflowId = taskListModel_.data(idx, TaskListModel::WorkflowIdRole).toString();
            task.workflowPath = taskListModel_.data(idx, TaskListModel::WorkflowPathRole).toString();
            task.status = taskListModel_.data(idx, TaskListModel::StatusRole).toString();
            task.attempt = taskListModel_.data(idx, TaskListModel::AttemptRole).toInt();
            task.maxAttempts = taskListModel_.data(idx, TaskListModel::MaxAttemptsRole).toInt();
            task.priority = taskListModel_.data(idx, TaskListModel::PriorityRole).toInt();
            task.exitCode = taskListModel_.data(idx, TaskListModel::ExitCodeRole).toInt();
            task.lastError = taskListModel_.data(idx, TaskListModel::LastErrorRole).toString();
            task.measurementName = taskListModel_.data(idx, TaskListModel::MeasurementNameRole).toString();
            task.measuredValue = taskListModel_.data(idx, TaskListModel::MeasuredValueRole).toString();
            task.measuredUnit = taskListModel_.data(idx, TaskListModel::MeasuredUnitRole).toString();
            task.lowerLimit = taskListModel_.data(idx, TaskListModel::LowerLimitRole).toString();
            task.upperLimit = taskListModel_.data(idx, TaskListModel::UpperLimitRole).toString();
            task.resultText = taskListModel_.data(idx, TaskListModel::ResultTextRole).toString();
            task.analyzeMessage = taskListModel_.data(idx, TaskListModel::AnalyzeMessageRole).toString();
            found = true;
            break;
        }
    }
    if (!found) {
        task.taskId = taskId;
    }

    task.cellId = obj.value("cellId").toInt(task.cellId >= 0 ? task.cellId : obj.value("slotId").toInt(0) - 1);
    task.workflowId = obj.value("workflowId").toString(task.workflowId);
    task.workflowPath = obj.value("workflowFilePath").toString(task.workflowPath);
    task.priority = obj.value("priority").toInt(task.priority);
    task.attempt = obj.value("attempt").toInt(task.attempt);
    task.maxAttempts = obj.value("maxAttempts").toInt(task.maxAttempts);
    task.lastError = obj.value("reason").toString(task.lastError);
    task.exitCode = obj.value("exitCode").toInt(task.exitCode);

    const QString event = obj.value("event").toString();
    if (event == "orchestration.task.started")           task.status = "running";
    else if (event == "orchestration.task.retry_scheduled") task.status = "retry_scheduled";
    else if (event == "orchestration.task.failed")       task.status = "failed";
    else if (event == "orchestration.task.skipped")      task.status = "skipped";
    else if (event == "orchestration.task.finished")     task.status = "succeeded";

    taskListModel_.updateTask(task);
}

} // namespace eon::studio
