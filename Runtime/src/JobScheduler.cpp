#include "eon/runtime/JobScheduler.h"

#include <QDebug>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>

namespace eon::runtime {

JobScheduler::~JobScheduler()
{
    stop();
}

int JobScheduler::submitJob(const std::string& workflowJson, const std::string& workflowId,
                             int priority, int maxRetries)
{
    std::lock_guard lock(jobsMutex_);
    int id = nextJobId_++;
    Job job;
    job.id = id;
    job.workflowJson = workflowJson;
    job.workflowId = workflowId;
    job.priority = priority;
    job.maxRetries = maxRetries;
    job.status = JobStatus::Pending;
    jobs_.push_back(std::move(job));
    return id;
}

JobScheduler::JobStatus JobScheduler::jobStatus(int jobId) const
{
    std::lock_guard lock(jobsMutex_);
    for (const auto& j : jobs_) {
        if (j.id == jobId) return j.status;
    }
    return JobStatus::Failed;
}

std::vector<JobScheduler::Job> JobScheduler::allJobs() const
{
    std::lock_guard lock(jobsMutex_);
    return jobs_;
}

bool JobScheduler::cancelJob(int jobId)
{
    std::lock_guard lock(jobsMutex_);
    for (auto& j : jobs_) {
        if (j.id == jobId) {
            if (j.status == JobStatus::Pending) {
                j.status = JobStatus::Failed;
                j.errorMessage = "cancelled";
                return true;
            }
            return false;
        }
    }
    return false;
}

void JobScheduler::registerAgent(const std::string& agentId, const std::string& host, int port)
{
    std::lock_guard lock(agentsMutex_);
    for (auto& a : agents_) {
        if (a.id == agentId) {
            a.host = host;
            a.port = port;
            a.lastHeartbeat = std::chrono::steady_clock::now();
            return;
        }
    }
    AgentInfo agent;
    agent.id = agentId;
    agent.host = host;
    agent.port = port;
    agent.online = true;
    agent.lastHeartbeat = std::chrono::steady_clock::now();
    agents_.push_back(std::move(agent));
}

std::vector<JobScheduler::AgentInfo> JobScheduler::onlineAgents() const
{
    std::lock_guard lock(agentsMutex_);
    std::vector<AgentInfo> result;
    auto now = std::chrono::steady_clock::now();
    for (const auto& a : agents_) {
        if (a.online && (now - a.lastHeartbeat) < std::chrono::seconds(15)) {
            result.push_back(a);
        }
    }
    return result;
}

void JobScheduler::start()
{
    if (running_) return;
    running_ = true;

    // 默认 HTTP 执行回调
    if (!executeCb_) {
        executeCb_ = [](const std::string& agentHost, const std::string& workflowJson, int port) -> std::string {
            QTcpSocket socket;
            socket.connectToHost(QString::fromStdString(agentHost), port);
            if (!socket.waitForConnected(5000)) return "{\"error\":\"connect failed\"}";

            // 发送作业 JSON
            QJsonObject request;
            request["action"] = "execute";
            request["workflow"] = QJsonDocument::fromJson(QByteArray::fromStdString(workflowJson)).object();
            QByteArray data = QJsonDocument(request).toJson(QJsonDocument::Compact);

            socket.write(data);
            if (!socket.waitForBytesWritten(3000)) return "{\"error\":\"write failed\"}";

            // 读取响应
            if (!socket.waitForReadyRead(30000)) return "{\"error\":\"timeout\"}";
            QByteArray response = socket.readAll();
            socket.close();
            return response.toStdString();
        };
    }

    dispatchThread_ = std::make_unique<std::thread>([this]() { dispatchLoop(); });
}

void JobScheduler::stop()
{
    running_ = false;
    if (dispatchThread_ && dispatchThread_->joinable()) {
        dispatchThread_->join();
    }
    dispatchThread_.reset();
}

void JobScheduler::dispatchLoop()
{
    while (running_) {
        // 查找待处理的作业
        Job* pendingJob = nullptr;
        {
            std::lock_guard lock(jobsMutex_);
            for (auto& j : jobs_) {
                if (j.status == JobStatus::Pending) {
                    pendingJob = &j;
                    break;
                }
            }
        }

        if (!pendingJob) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // 查找可用 Agent
        auto* agentInfo = findIdleAgent();
        if (!agentInfo) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // 标记为运行中
        {
            std::lock_guard lock(jobsMutex_);
            pendingJob->status = JobStatus::Running;
            pendingJob->attempt++;
        }

        qDebug().noquote() << QString("[JobScheduler] Dispatching job %1 to agent %2")
                                  .arg(QString::number(pendingJob->id))
                                  .arg(QString::fromStdString(agentInfo->id));

        // 发送到 Agent
        std::string result;
        try {
            result = executeCb_(agentInfo->host, pendingJob->workflowJson, agentInfo->port);
        } catch (const std::exception& e) {
            result = "{\"error\":\"" + std::string(e.what()) + "\"}";
        }

        // 解析结果
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(result));
        bool success = doc.isObject() && doc.object().value("status").toString() == "ok";

        if (success) {
            completeJob(pendingJob->id, result);
        } else {
            if (pendingJob->attempt < pendingJob->maxRetries) {
                // 重试
                std::lock_guard lock(jobsMutex_);
                pendingJob->status = JobStatus::Pending;
                qDebug().noquote() << QString("[JobScheduler] Job %1 failed, retry %2/%3")
                                          .arg(QString::number(pendingJob->id)).arg(pendingJob->attempt).arg(pendingJob->maxRetries);
            } else {
                failJob(pendingJob->id, result);
            }
        }
    }
}

JobScheduler::AgentInfo* JobScheduler::findIdleAgent()
{
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(agentsMutex_);
    for (auto& a : agents_) {
        bool healthy = a.online && (now - a.lastHeartbeat) < std::chrono::seconds(15);
        if (healthy) return &a;
    }
    return nullptr;
}

void JobScheduler::completeJob(int jobId, const std::string& result)
{
    std::lock_guard lock(jobsMutex_);
    for (auto& j : jobs_) {
        if (j.id == jobId) {
            j.status = JobStatus::Succeeded;
            j.resultJson = result;
            qDebug().noquote() << QString("[JobScheduler] Job %1 completed").arg(jobId);
            return;
        }
    }
}

void JobScheduler::failJob(int jobId, const std::string& error)
{
    std::lock_guard lock(jobsMutex_);
    for (auto& j : jobs_) {
        if (j.id == jobId) {
            j.status = JobStatus::Failed;
            j.errorMessage = error;
            qDebug().noquote() << QString("[JobScheduler] Job %1 failed: %2")
                                      .arg(jobId).arg(QString::fromStdString(error));
            return;
        }
    }
}

} // namespace eon::runtime
