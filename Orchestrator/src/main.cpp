#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QtGlobal>

namespace {

enum class TaskStatus {
    Pending,
    Running,
    Succeeded,
    Failed
};

struct WorkflowTask {
    int taskId = 0;
    QString workflowPath;
    QString workflowId;
    QStringList resourceLocks;
    int priority = 0;
    qint64 enqueueOrder = 0;
    int attemptsMade = 0;
    int maxRetries = 1;
    qint64 nextRunAtMs = 0;
    int lastExitCode = 0;
    QString lastError;
    TaskStatus status = TaskStatus::Pending;
};

struct RunningTask {
    int slotId = 0;
    int taskId = 0;
    QProcess* process = nullptr;
};

QString statusToString(TaskStatus status) {
    switch (status) {
    case TaskStatus::Pending:
        return "pending";
    case TaskStatus::Running:
        return "running";
    case TaskStatus::Succeeded:
        return "succeeded";
    case TaskStatus::Failed:
        return "failed";
    }
    return "pending";
}

TaskStatus stringToStatus(const QString& text) {
    if (text == "running") {
        return TaskStatus::Running;
    }
    if (text == "succeeded") {
        return TaskStatus::Succeeded;
    }
    if (text == "failed") {
        return TaskStatus::Failed;
    }
    return TaskStatus::Pending;
}

void emitTelemetry(const QString& eventName, const QJsonObject& fields = {}) {
    QJsonObject payload{
        {"source", "orchestrator"},
        {"event", eventName},
        {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
    };

    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        payload.insert(it.key(), it.value());
    }

    qInfo().noquote() << QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

QJsonArray toJsonArray(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QStringList jsonArrayToStringList(const QJsonArray& array) {
    QStringList values;
    for (const auto& value : array) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty() && !values.contains(text)) {
            values.append(text);
        }
    }
    return values;
}

bool parseWorkflowTaskMetadata(WorkflowTask* task, QString* errorMessage) {
    if (task == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Task is null.";
        }
        return false;
    }

    if (task->workflowPath.isEmpty()) {
        task->workflowId = "mvp-workflow";
        task->resourceLocks.clear();
        task->priority = 0;
        return true;
    }

    QFile file(task->workflowPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Cannot open workflow file '%1'.").arg(task->workflowPath);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Invalid workflow file '%1': %2")
                                .arg(task->workflowPath, parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = doc.object();
    task->workflowId = root.value("workflowId").toString().trimmed();
    if (task->workflowId.isEmpty()) {
        task->workflowId = QFileInfo(task->workflowPath).baseName();
    }
    task->resourceLocks = jsonArrayToStringList(root.value("resourceLocks").toArray());
    task->priority = root.value("priority").toInt(0);
    return true;
}

bool shouldRetry(const WorkflowTask& task) {
    return task.attemptsMade < (task.maxRetries + 1);
}

bool isTaskRunnable(const WorkflowTask& task, const QSet<QString>& heldLocks, qint64 nowMs) {
    if (task.status != TaskStatus::Pending) {
        return false;
    }
    if (task.nextRunAtMs > nowMs) {
        return false;
    }
    for (const QString& lockName : task.resourceLocks) {
        if (heldLocks.contains(lockName)) {
            return false;
        }
    }
    return true;
}

QJsonObject taskToJson(const WorkflowTask& task) {
    return QJsonObject{
        {"taskId", task.taskId},
        {"workflowPath", task.workflowPath},
        {"workflowId", task.workflowId},
        {"resourceLocks", toJsonArray(task.resourceLocks)},
        {"priority", task.priority},
        {"enqueueOrder", QString::number(task.enqueueOrder)},
        {"attemptsMade", task.attemptsMade},
        {"maxRetries", task.maxRetries},
        {"nextRunAtMs", QString::number(task.nextRunAtMs)},
        {"lastExitCode", task.lastExitCode},
        {"lastError", task.lastError},
        {"status", statusToString(task.status)}
    };
}

WorkflowTask taskFromJson(const QJsonObject& object) {
    WorkflowTask task;
    task.taskId = object.value("taskId").toInt();
    task.workflowPath = object.value("workflowPath").toString();
    task.workflowId = object.value("workflowId").toString();
    task.resourceLocks = jsonArrayToStringList(object.value("resourceLocks").toArray());
    task.priority = object.value("priority").toInt(0);
    task.enqueueOrder = object.value("enqueueOrder").toString().toLongLong();
    task.attemptsMade = object.value("attemptsMade").toInt();
    task.maxRetries = qMax(0, object.value("maxRetries").toInt(1));
    task.nextRunAtMs = object.value("nextRunAtMs").toString().toLongLong();
    task.lastExitCode = object.value("lastExitCode").toInt();
    task.lastError = object.value("lastError").toString();
    task.status = stringToStatus(object.value("status").toString());
    return task;
}

int selectRunnableTaskIndex(const QList<WorkflowTask>& tasks, const QSet<QString>& heldLocks, qint64 nowMs) {
    int selectedIndex = -1;
    for (int i = 0; i < tasks.size(); ++i) {
        if (!isTaskRunnable(tasks.at(i), heldLocks, nowMs)) {
            continue;
        }
        if (selectedIndex < 0) {
            selectedIndex = i;
            continue;
        }

        const WorkflowTask& candidate = tasks.at(i);
        const WorkflowTask& selected = tasks.at(selectedIndex);
        if (candidate.priority != selected.priority) {
            if (candidate.priority > selected.priority) {
                selectedIndex = i;
            }
            continue;
        }
        if (candidate.nextRunAtMs != selected.nextRunAtMs) {
            if (candidate.nextRunAtMs < selected.nextRunAtMs) {
                selectedIndex = i;
            }
            continue;
        }
        if (candidate.enqueueOrder < selected.enqueueOrder) {
            selectedIndex = i;
        }
    }
    return selectedIndex;
}

bool saveSchedulerState(
    const QString& stateFilePath,
    const QString& pluginDirectory,
    int cellCount,
    int defaultMaxTaskRetries,
    int retryBackoffMs,
    const QList<WorkflowTask>& tasks,
    QString* errorMessage
) {
    QJsonArray taskArray;
    for (const auto& task : tasks) {
        taskArray.append(taskToJson(task));
    }

    const QJsonObject root{
        {"version", 1},
        {"updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"pluginDirectory", pluginDirectory},
        {"cellCount", cellCount},
        {"defaultMaxTaskRetries", defaultMaxTaskRetries},
        {"retryBackoffMs", retryBackoffMs},
        {"tasks", taskArray}
    };

    QSaveFile stateFile(stateFilePath);
    if (!stateFile.open(QIODevice::WriteOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Cannot open scheduler state file '%1' for writing.").arg(stateFilePath);
        }
        return false;
    }
    stateFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!stateFile.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Cannot commit scheduler state file '%1'.").arg(stateFilePath);
        }
        return false;
    }
    return true;
}

bool loadSchedulerState(
    const QString& stateFilePath,
    QString* pluginDirectory,
    int* savedCellCount,
    int* savedDefaultMaxTaskRetries,
    int* savedRetryBackoffMs,
    QList<WorkflowTask>* tasks,
    QString* errorMessage
) {
    if (tasks == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Task list output is null.";
        }
        return false;
    }

    QFile stateFile(stateFilePath);
    if (!stateFile.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Cannot open scheduler state file '%1'.").arg(stateFilePath);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(stateFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Invalid scheduler state file '%1': %2")
                                .arg(stateFilePath, parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = doc.object();
    if (pluginDirectory != nullptr) {
        *pluginDirectory = root.value("pluginDirectory").toString().trimmed();
    }
    if (savedCellCount != nullptr) {
        *savedCellCount = root.value("cellCount").toInt(1);
    }
    if (savedDefaultMaxTaskRetries != nullptr) {
        *savedDefaultMaxTaskRetries = root.value("defaultMaxTaskRetries").toInt(1);
    }
    if (savedRetryBackoffMs != nullptr) {
        *savedRetryBackoffMs = root.value("retryBackoffMs").toInt(300);
    }

    tasks->clear();
    const QJsonArray taskArray = root.value("tasks").toArray();
    for (const auto& value : taskArray) {
        if (value.isObject()) {
            tasks->append(taskFromJson(value.toObject()));
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    QString pluginDirectory;
    QStringList workflowFilePaths;
    int cellCount = 1;
    bool cellCountSpecified = false;
    bool resumeFromState = false;
    int defaultMaxTaskRetries = 1;
    bool maxRetriesSpecified = false;
    int retryBackoffMs = 300;
    bool retryBackoffSpecified = false;
    bool stopOnFailure = false;
    QString stateFilePath = QDir(QCoreApplication::applicationDirPath()).filePath("orchestration-state.json");

    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == "--cells") {
            if (i + 1 >= argc) {
                qCritical() << "--cells requires a positive integer value.";
                return 2;
            }
            bool ok = false;
            const int value = QString::fromLocal8Bit(argv[++i]).toInt(&ok);
            if (!ok || value <= 0) {
                qCritical() << "Invalid --cells value:" << value;
                return 2;
            }
            cellCount = value;
            cellCountSpecified = true;
            continue;
        }
        if (argument == "--resume") {
            resumeFromState = true;
            continue;
        }
        if (argument == "--state") {
            if (i + 1 >= argc) {
                qCritical() << "--state requires a file path.";
                return 2;
            }
            stateFilePath = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (argument == "--max-task-retries") {
            if (i + 1 >= argc) {
                qCritical() << "--max-task-retries requires an integer value.";
                return 2;
            }
            bool ok = false;
            const int value = QString::fromLocal8Bit(argv[++i]).toInt(&ok);
            if (!ok || value < 0) {
                qCritical() << "Invalid --max-task-retries value:" << value;
                return 2;
            }
            defaultMaxTaskRetries = value;
            maxRetriesSpecified = true;
            continue;
        }
        if (argument == "--retry-backoff-ms") {
            if (i + 1 >= argc) {
                qCritical() << "--retry-backoff-ms requires an integer value.";
                return 2;
            }
            bool ok = false;
            const int value = QString::fromLocal8Bit(argv[++i]).toInt(&ok);
            if (!ok || value < 0) {
                qCritical() << "Invalid --retry-backoff-ms value:" << value;
                return 2;
            }
            retryBackoffMs = value;
            retryBackoffSpecified = true;
            continue;
        }
        if (argument == "--stop-on-failure") {
            stopOnFailure = true;
            continue;
        }

        if (pluginDirectory.isEmpty()) {
            pluginDirectory = argument;
        } else {
            workflowFilePaths.append(argument);
        }
    }

    const QString workerFileName =
#if defined(Q_OS_WIN)
        "eon-runtime-worker.exe";
#else
        "eon-runtime-worker";
#endif
    const QString workerPath = QDir(QCoreApplication::applicationDirPath()).filePath(workerFileName);
    if (!QFileInfo::exists(workerPath)) {
        qCritical() << "Runtime worker not found:" << workerPath;
        return 10;
    }

    QList<WorkflowTask> allTasks;
    int taskCounter = 0;
    int tasksSucceeded = 0;
    int tasksFailed = 0;

    if (resumeFromState) {
        QString statePluginDirectory;
        int savedCellCount = cellCount;
        int savedRetryCount = defaultMaxTaskRetries;
        int savedBackoff = retryBackoffMs;
        QString loadError;
        if (!loadSchedulerState(
                stateFilePath,
                &statePluginDirectory,
                &savedCellCount,
                &savedRetryCount,
                &savedBackoff,
                &allTasks,
                &loadError
            )) {
            qCritical() << loadError;
            return 4;
        }
        if (pluginDirectory.isEmpty()) {
            pluginDirectory = statePluginDirectory;
        }
        if (!cellCountSpecified) {
            cellCount = qMax(1, savedCellCount);
        }
        if (!maxRetriesSpecified) {
            defaultMaxTaskRetries = savedRetryCount;
        }
        if (!retryBackoffSpecified) {
            retryBackoffMs = savedBackoff;
        }
        for (auto& task : allTasks) {
            taskCounter = qMax(taskCounter, task.taskId);
            if (task.enqueueOrder <= 0) {
                task.enqueueOrder = task.taskId;
            }
            if (task.status == TaskStatus::Succeeded) {
                tasksSucceeded += 1;
                continue;
            }
            if (task.status == TaskStatus::Failed && !shouldRetry(task)) {
                tasksFailed += 1;
                continue;
            }
            task.status = TaskStatus::Pending;
        }
    }

    if (pluginDirectory.isEmpty()) {
        pluginDirectory = QDir(QCoreApplication::applicationDirPath()).filePath("../Plugins");
    }

    for (const QString& workflowPath : workflowFilePaths) {
        WorkflowTask task;
        task.taskId = ++taskCounter;
        task.enqueueOrder = task.taskId;
        task.workflowPath = workflowPath;
        task.maxRetries = defaultMaxTaskRetries;
        QString error;
        if (!parseWorkflowTaskMetadata(&task, &error)) {
            qCritical() << error;
            return 3;
        }
        allTasks.append(task);
    }

    if (!resumeFromState && workflowFilePaths.isEmpty()) {
        WorkflowTask task;
        task.taskId = ++taskCounter;
        task.enqueueOrder = task.taskId;
        task.maxRetries = defaultMaxTaskRetries;
        QString error;
        if (!parseWorkflowTaskMetadata(&task, &error)) {
            qCritical() << error;
            return 3;
        }
        allTasks.append(task);
    }

    emitTelemetry("orchestration.started", {
        {"workerPath", workerPath},
        {"pluginDirectory", pluginDirectory},
        {"cellCount", cellCount},
        {"taskCount", allTasks.size()},
        {"resume", resumeFromState},
        {"stateFilePath", stateFilePath},
        {"maxTaskRetries", defaultMaxTaskRetries},
        {"retryBackoffMs", retryBackoffMs},
        {"stopOnFailure", stopOnFailure}
    });

    QString saveError;
    if (!saveSchedulerState(
            stateFilePath,
            pluginDirectory,
            cellCount,
            defaultMaxTaskRetries,
            retryBackoffMs,
            allTasks,
            &saveError
        )) {
        qCritical() << saveError;
        return 5;
    }

    auto flushProcessOutput = [](QProcess* process) {
        if (process == nullptr) {
            return;
        }
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed();
        if (!output.isEmpty()) {
            qInfo().noquote() << output;
        }
    };

    auto findTaskIndexById = [&](int taskId) -> int {
        for (int i = 0; i < allTasks.size(); ++i) {
            if (allTasks[i].taskId == taskId) {
                return i;
            }
        }
        return -1;
    };

    QElapsedTimer timer;
    timer.start();
    QList<RunningTask> runningTasks;
    QSet<QString> heldLocks;
    int nextSlotId = 1;
    bool schedulingHalted = false;
    auto haltPendingTasksAfterFailure = [&](const WorkflowTask& failedTask) {
        if (!stopOnFailure || schedulingHalted) {
            return;
        }
        schedulingHalted = true;
        emitTelemetry("orchestration.scheduling.halted", {
            {"reason", "stop_on_failure"},
            {"failedTaskId", failedTask.taskId},
            {"failedWorkflowId", failedTask.workflowId},
            {"failedWorkflowFilePath", failedTask.workflowPath},
            {"priority", failedTask.priority}
        });
        for (auto& pendingTask : allTasks) {
            if (pendingTask.status != TaskStatus::Pending) {
                continue;
            }
            pendingTask.status = TaskStatus::Failed;
            pendingTask.lastExitCode = -2;
            pendingTask.lastError = QString("Skipped because --stop-on-failure was triggered by task %1.")
                                        .arg(failedTask.taskId);
            pendingTask.nextRunAtMs = 0;
            tasksFailed += 1;
            emitTelemetry("orchestration.task.skipped", {
                {"taskId", pendingTask.taskId},
                {"workflowId", pendingTask.workflowId},
                {"workflowFilePath", pendingTask.workflowPath},
                {"priority", pendingTask.priority},
                {"reason", "stop_on_failure"},
                {"failedByTaskId", failedTask.taskId}
            });
        }
    };

    auto startRunnableTasks = [&]() {
        if (schedulingHalted) {
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        while (runningTasks.size() < cellCount) {
            int runnableIndex = selectRunnableTaskIndex(allTasks, heldLocks, nowMs);
            if (runnableIndex < 0) {
                break;
            }

            WorkflowTask& task = allTasks[runnableIndex];
            QProcess* worker = new QProcess(&app);
            worker->setProgram(workerPath);
            QStringList workerArgs{pluginDirectory};
            if (!task.workflowPath.isEmpty()) {
                workerArgs.append(task.workflowPath);
            }
            worker->setArguments(workerArgs);
            worker->setProcessChannelMode(QProcess::MergedChannels);
            worker->start();

            task.attemptsMade += 1;
            task.status = TaskStatus::Running;
            task.lastExitCode = 0;
            task.lastError.clear();
            task.nextRunAtMs = 0;

            if (!worker->waitForStarted(5000)) {
                task.status = TaskStatus::Failed;
                task.lastError = "Failed to start runtime worker process.";
                task.lastExitCode = -1;

                if (shouldRetry(task)) {
                    task.status = TaskStatus::Pending;
                    task.nextRunAtMs =
                        QDateTime::currentMSecsSinceEpoch() + (retryBackoffMs * task.attemptsMade);
                    emitTelemetry("orchestration.task.retry_scheduled", {
                        {"taskId", task.taskId},
                        {"workflowId", task.workflowId},
                        {"workflowFilePath", task.workflowPath},
                        {"priority", task.priority},
                        {"attempt", task.attemptsMade},
                        {"maxAttempts", task.maxRetries + 1},
                        {"nextRunAtMs", QString::number(task.nextRunAtMs)},
                        {"reason", task.lastError}
                    });
                } else {
                    tasksFailed += 1;
                    emitTelemetry("orchestration.task.failed", {
                        {"taskId", task.taskId},
                        {"workflowId", task.workflowId},
                        {"workflowFilePath", task.workflowPath},
                        {"priority", task.priority},
                        {"attempt", task.attemptsMade},
                        {"maxAttempts", task.maxRetries + 1},
                        {"reason", task.lastError}
                    });
                    haltPendingTasksAfterFailure(task);
                }

                QString persistError;
                if (!saveSchedulerState(
                        stateFilePath,
                        pluginDirectory,
                        cellCount,
                        defaultMaxTaskRetries,
                        retryBackoffMs,
                        allTasks,
                        &persistError
                    )) {
                    qCritical() << persistError;
                }

                worker->deleteLater();
                continue;
            }

            for (const QString& lockName : task.resourceLocks) {
                heldLocks.insert(lockName);
            }

            const int slotId = nextSlotId++;
            runningTasks.append(RunningTask{
                .slotId = slotId,
                .taskId = task.taskId,
                .process = worker
            });

            emitTelemetry("orchestration.task.started", {
                {"taskId", task.taskId},
                {"slotId", slotId},
                {"workflowId", task.workflowId},
                {"workflowFilePath", task.workflowPath},
                {"resourceLocks", toJsonArray(task.resourceLocks)},
                {"priority", task.priority},
                {"attempt", task.attemptsMade},
                {"maxAttempts", task.maxRetries + 1}
            });

            QString persistError;
            if (!saveSchedulerState(
                    stateFilePath,
                    pluginDirectory,
                    cellCount,
                    defaultMaxTaskRetries,
                    retryBackoffMs,
                    allTasks,
                    &persistError
                )) {
                qCritical() << persistError;
            }
        }
    };

    auto hasPendingWork = [&]() {
        for (const auto& task : allTasks) {
            if (task.status == TaskStatus::Pending || task.status == TaskStatus::Running) {
                return true;
            }
        }
        return false;
    };

    startRunnableTasks();
    while (hasPendingWork() || !runningTasks.isEmpty()) {
        for (int index = runningTasks.size() - 1; index >= 0; --index) {
            RunningTask& running = runningTasks[index];
            flushProcessOutput(running.process);
            if (running.process->state() != QProcess::NotRunning) {
                continue;
            }

            flushProcessOutput(running.process);
            const int taskIndex = findTaskIndexById(running.taskId);
            if (taskIndex < 0) {
                running.process->deleteLater();
                runningTasks.removeAt(index);
                continue;
            }

            WorkflowTask& task = allTasks[taskIndex];
            const int exitCode = running.process->exitCode();
            const bool success =
                (running.process->exitStatus() == QProcess::NormalExit) && (exitCode == 0);

            if (success) {
                task.status = TaskStatus::Succeeded;
                task.lastExitCode = exitCode;
                task.lastError.clear();
                tasksSucceeded += 1;
            } else {
                task.status = TaskStatus::Failed;
                task.lastExitCode = exitCode;
                task.lastError = QString("Worker exit code %1.").arg(exitCode);
                if (shouldRetry(task)) {
                    task.status = TaskStatus::Pending;
                    task.nextRunAtMs =
                        QDateTime::currentMSecsSinceEpoch() + (retryBackoffMs * task.attemptsMade);
                    emitTelemetry("orchestration.task.retry_scheduled", {
                        {"taskId", task.taskId},
                        {"slotId", running.slotId},
                        {"workflowId", task.workflowId},
                        {"workflowFilePath", task.workflowPath},
                        {"priority", task.priority},
                        {"attempt", task.attemptsMade},
                        {"maxAttempts", task.maxRetries + 1},
                        {"nextRunAtMs", QString::number(task.nextRunAtMs)},
                        {"reason", task.lastError}
                    });
                } else {
                    tasksFailed += 1;
                    haltPendingTasksAfterFailure(task);
                }
            }

            emitTelemetry("orchestration.task.finished", {
                {"taskId", task.taskId},
                {"slotId", running.slotId},
                {"workflowId", task.workflowId},
                {"workflowFilePath", task.workflowPath},
                {"priority", task.priority},
                {"status", success ? "ok" : (task.status == TaskStatus::Pending ? "retrying" : "failed")},
                {"exitCode", exitCode},
                {"normalExit", running.process->exitStatus() == QProcess::NormalExit},
                {"attempt", task.attemptsMade},
                {"maxAttempts", task.maxRetries + 1}
            });

            for (const QString& lockName : task.resourceLocks) {
                heldLocks.remove(lockName);
            }
            running.process->deleteLater();
            runningTasks.removeAt(index);

            QString persistError;
            if (!saveSchedulerState(
                    stateFilePath,
                    pluginDirectory,
                    cellCount,
                    defaultMaxTaskRetries,
                    retryBackoffMs,
                    allTasks,
                    &persistError
                )) {
                qCritical() << persistError;
            }
        }

        startRunnableTasks();
        QCoreApplication::processEvents();
        QThread::msleep(15);
    }

    const bool success = (tasksFailed == 0);
    emitTelemetry("orchestration.finished", {
        {"status", success ? "ok" : "failed"},
        {"tasksTotal", allTasks.size()},
        {"tasksSucceeded", tasksSucceeded},
        {"tasksFailed", tasksFailed},
        {"cellCount", cellCount},
        {"durationMs", timer.elapsed()},
        {"normalExit", true},
        {"stateFilePath", stateFilePath}
    });

    QString finalPersistError;
    if (!saveSchedulerState(
            stateFilePath,
            pluginDirectory,
            cellCount,
            defaultMaxTaskRetries,
            retryBackoffMs,
            allTasks,
            &finalPersistError
        )) {
        qCritical() << finalPersistError;
        return 6;
    }

    if (!success) {
        qCritical() << "Orchestration completed with failed tasks:" << tasksFailed;
        return 12;
    }

    qInfo() << "Orchestration completed.";
    return 0;
}
