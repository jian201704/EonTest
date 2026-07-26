#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <QString>
#include <QJsonObject>

namespace eon::runtime {

/// <summary>
/// 作业调度器（P2 分布式 Runner/Agent）。
/// 管理作业队列，分发到远程 Agent，处理重试/失败。
///
/// 架构：
///   JobScheduler (调度端)
///     ├─ 作业队列 (priority queue)
///     ├─ Agent 管理器 (健康检测)
///     └─ 分发器 (派发作业到可用 Agent)
///           │
///           ▼
///   RemoteAgent (远程执行端)
///     ├─ 接收作业 JSON
///     ├─ 执行 Workflow
///     └─ 返回结果 JSON
/// </summary>
class JobScheduler {
public:
    JobScheduler() = default;
    ~JobScheduler();

    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;

    /// 作业状态
    enum class JobStatus { Pending, Running, Succeeded, Failed };

    struct Job {
        int id = 0;
        std::string workflowJson;
        std::string workflowId;
        int priority = 0;
        int maxRetries = 3;
        int attempt = 0;
        JobStatus status = JobStatus::Pending;
        std::string resultJson;
        std::string errorMessage;
    };

    struct AgentInfo {
        std::string id;
        std::string host;
        int port = 0;
        bool online = false;
        std::chrono::steady_clock::time_point lastHeartbeat;
    };

    // ============================================================
    // 作业管理
    // ============================================================

    /// 提交作业
    int submitJob(const std::string& workflowJson, const std::string& workflowId,
                   int priority = 0, int maxRetries = 3);

    /// 获取作业状态
    JobStatus jobStatus(int jobId) const;

    /// 获取所有作业
    std::vector<Job> allJobs() const;

    /// 取消作业
    bool cancelJob(int jobId);

    // ============================================================
    // Agent 管理
    // ============================================================

    /// 注册 Agent
    void registerAgent(const std::string& agentId, const std::string& host, int port);

    /// 获取在线 Agent 列表
    std::vector<AgentInfo> onlineAgents() const;

    // ============================================================
    // 生命周期
    // ============================================================

    /// 启动调度器（开始派发作业）
    void start();

    /// 停止调度器
    void stop();

    /// 设置作业执行回调（默认用 HTTP 发送到 Agent）
    using ExecuteCallback = std::function<std::string(const std::string&, const std::string&, int)>;
    void setExecuteCallback(ExecuteCallback cb) { executeCb_ = std::move(cb); }

private:
    void dispatchLoop();
    AgentInfo* findIdleAgent();
    void completeJob(int jobId, const std::string& result);
    void failJob(int jobId, const std::string& error);

    // 作业队列
    std::vector<Job> jobs_;
    mutable std::mutex jobsMutex_;
    int nextJobId_ = 1;

    // Agent 列表
    std::vector<AgentInfo> agents_;
    mutable std::mutex agentsMutex_;

    // 调度线程
    std::unique_ptr<std::thread> dispatchThread_;
    std::atomic<bool> running_{false};

    // 回调
    ExecuteCallback executeCb_;
};

} // namespace eon::runtime
