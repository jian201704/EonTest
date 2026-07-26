#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <QDir>
#include <QString>

namespace eon::runtime {

/// <summary>
/// CELL 工作单元（参考章节 6 per-cell Worker 设计）。
/// 每个 CELL 对应一个独立的工作线程，可执行工作流步骤。
/// Engine 通过 CellWorker 实现多 CELL 并行执行、日志隔离、心跳健康监测。
///
/// P0 实现：线程内 Worker（轻量）
/// P1 可演进为：独立进程 Worker（gRPC/IPC）
/// </summary>
class CellWorker {
public:
    /// 步骤执行请求
    struct StepRequest {
        std::string stepId;
        std::string pluginId;
        std::string workflowId;
        int attempt = 1;
    };

    /// 步骤执行结果
    struct StepResult {
        std::string stepId;
        bool success = false;
        std::string errorMessage;
        qint64 elapsedMs = 0;
    };

    /// Worker 状态
    enum class State {
        Idle,       ///< 空闲，等待任务
        Running,    ///< 正在执行步骤
        Error,      ///< 执行出错
        Dead        ///< 心跳超时 / 崩溃
    };

    explicit CellWorker(const QString& cellId, const QString& reportsBaseDir);
    ~CellWorker();

    /// 不允许复制
    CellWorker(const CellWorker&) = delete;
    CellWorker& operator=(const CellWorker&) = delete;

    /// CELL ID
    QString cellId() const { return cellId_; }

    /// 当前状态
    State state() const { return state_.load(); }

    /// 获取报告目录
    QString reportsDir() const { return reportsDir_; }

    // ============================================================
    // 生命周期
    // ============================================================

    /// 启动工作线程
    bool start();

    /// 停止工作线程
    void stop();

    /// 重启工作线程
    bool restart();

    /// 是否正在运行
    bool isRunning() const { return thread_ && thread_->joinable(); }

    /// 是否健康（心跳未超时）
    bool isHealthy() const;

    // ============================================================
    // 任务提交
    // ============================================================

    /// 提交步骤执行请求（异步）
    void submitStep(StepRequest request, std::function<void(StepResult)> callback);

    /// 获取最近一次心跳时间
    std::chrono::steady_clock::time_point lastHeartbeat() const { return lastHeartbeat_; }

    /// 设置心跳超时（毫秒）
    void setHeartbeatTimeout(std::chrono::milliseconds timeout) { heartbeatTimeout_ = timeout; }

    /// 已执行的步骤数
    int completedSteps() const { return completedSteps_.load(); }

private:
    /// 工作线程主循环
    void workerLoop();

    /// 执行单个步骤
    StepResult executeStep(const StepRequest& req);

    QString cellId_;
    QString reportsDir_;
    std::chrono::milliseconds heartbeatTimeout_{ 6000 }; // 默认 6s

    // 线程
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{ false };
    std::atomic<State> state_{ State::Idle };

    // 任务队列
    struct PendingTask {
        StepRequest request;
        std::function<void(StepResult)> callback;
    };
    std::queue<PendingTask> taskQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCv_;

    // 心跳
    std::atomic<std::chrono::steady_clock::time_point> lastHeartbeat_;
    std::atomic<int> completedSteps_{ 0 };
};

} // namespace eon::runtime
