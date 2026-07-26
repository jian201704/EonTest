#include "eon/runtime/CellWorker.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace eon::runtime {

CellWorker::CellWorker(const QString& cellId, const QString& reportsBaseDir)
    : cellId_(cellId)
{
    // 创建隔离的 reports 目录
    reportsDir_ = reportsBaseDir + "/" + cellId_;
    QDir().mkpath(reportsDir_);

    // 写入 cell 信息
    QJsonObject info;
    info["cellId"] = cellId_;
    info["created"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QFile infoFile(reportsDir_ + "/cell.info.json");
    if (infoFile.open(QIODevice::WriteOnly)) {
        infoFile.write(QJsonDocument(info).toJson(QJsonDocument::Indented));
        infoFile.close();
    }

    lastHeartbeat_.store(std::chrono::steady_clock::now());
}

CellWorker::~CellWorker()
{
    stop();
}

bool CellWorker::start()
{
    if (thread_ && thread_->joinable()) return false;

    running_ = true;
    state_.store(State::Idle);
    lastHeartbeat_.store(std::chrono::steady_clock::now());

    thread_ = std::make_unique<std::thread>(&CellWorker::workerLoop, this);
    qDebug().noquote() << QString("[CellWorker %1] started, reports=%2").arg(cellId_, reportsDir_);
    return true;
}

void CellWorker::stop()
{
    running_ = false;
    {
        std::lock_guard lock(queueMutex_);
        queueCv_.notify_all();
    }
    if (thread_ && thread_->joinable()) {
        thread_->join();
        thread_.reset();
    }
    state_.store(State::Idle);
    qDebug().noquote() << QString("[CellWorker %1] stopped").arg(cellId_);
}

bool CellWorker::restart()
{
    stop();
    return start();
}

bool CellWorker::isHealthy() const
{
    auto now = std::chrono::steady_clock::now();
    auto hb = lastHeartbeat_.load();
    return (now - hb) < heartbeatTimeout_;
}

void CellWorker::submitStep(StepRequest request, std::function<void(StepResult)> callback)
{
    std::lock_guard lock(queueMutex_);
    taskQueue_.push({std::move(request), std::move(callback)});
    queueCv_.notify_one();
}

// ============================================================
// 工作线程主循环
// ============================================================
void CellWorker::workerLoop()
{
    while (running_) {
        PendingTask task;
        bool hasTask = false;

        {
            std::unique_lock lock(queueMutex_);
            if (taskQueue_.empty()) {
                // 无任务时也定期更新心跳
                queueCv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                    return !taskQueue_.empty() || !running_;
                });
            }

            if (!taskQueue_.empty()) {
                task = std::move(taskQueue_.front());
                taskQueue_.pop();
                hasTask = true;
            }
        }

        // 更新心跳（即使无任务也定期标记存活）
        lastHeartbeat_.store(std::chrono::steady_clock::now());

        if (hasTask) {
            state_.store(State::Running);

            // 执行步骤
            StepResult result = executeStep(task.request);

            // 回调通知结果
            if (task.callback) {
                task.callback(result);
            }

            completedSteps_++;
            state_.store(State::Idle);
            lastHeartbeat_.store(std::chrono::steady_clock::now());
        }
    }
}

CellWorker::StepResult CellWorker::executeStep(const StepRequest& req)
{
    StepResult result;
    result.stepId = req.stepId;

    // P0：CellWorker 本身不执行步骤逻辑，通过回调委派给外部执行器
    // 实际步骤执行由 WorkflowEngine 提供 executor 完成
    // 此处留空 — 由集成方设置外部 executor
    //
    // 设计说明：
    // CellWorker 的职责是 隔离运行环境 + 心跳健康监测 + 日志目录隔离，
    // 而非直接执行步骤。实际步骤执行由集成者注入 StepExecutor。
    // 参见 WorkflowEngine 中的 CellWorker 集成。

    return result;
}

} // namespace eon::runtime
