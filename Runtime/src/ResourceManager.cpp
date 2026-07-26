#include "eon/sdk/ResourceManager.h"
#include "eon/sdk/IResource.h"
#include "eon/sdk/IScpiIO.h"

#include <algorithm>
#include <cassert>
#include <shared_mutex>

namespace eon::sdk {

namespace {

/// 代理 IResource — 包装 IScpiIO 使其适配 IResource 接口
class IoResourceProxy final : public IResource {
public:
    explicit IoResourceProxy(const QString& name, IScpiIO* io)
        : name_(name), io_(io), connected_(false) {}

    bool open() override {
        if (!io_) return false;
        if (io_->isConnected()) {
            connected_ = true;
            return true;
        }
        // IoResourceProxy 假设 IScpiIO 已由外部打开（由 ScpiStepPlugin 管理）
        // 如果未打开，尝试用空配置打开（适用于保留连接池语义）
        QVariantMap emptyCfg;
        connected_ = io_->open(emptyCfg);
        return connected_;
    }

    void close() override {
        if (io_) {
            io_->close();
        }
        connected_ = false;
    }

    QString name() const override { return name_; }
    bool isConnected() const override { return connected_ || (io_ && io_->isConnected()); }
    IScpiIO* scpiIO() const { return io_; }

private:
    QString name_;
    IScpiIO* io_;
    bool connected_;
};

} // anonymous namespace

// ==================================================================
// Lease 实现
// ==================================================================

Lease::Lease(const std::string& resourceId, LeaseMode mode,
             IResource* resource, IScpiIO* scpiIO,
             int leaseId, std::function<void(int)> releaser)
    : resourceId_(resourceId)
    , mode_(mode)
    , resource_(resource)
    , scpiIO_(scpiIO)
    , leaseId_(leaseId)
    , valid_(true)
    , releaser_(std::move(releaser))
{
}

Lease::~Lease()
{
    release();
}

Lease::Lease(Lease&& other) noexcept
    : resourceId_(std::move(other.resourceId_))
    , mode_(other.mode_)
    , resource_(other.resource_)
    , scpiIO_(other.scpiIO_)
    , leaseId_(other.leaseId_)
    , valid_(other.valid_)
    , releaser_(std::move(other.releaser_))
{
    other.valid_ = false;
    other.leaseId_ = -1;
    other.resource_ = nullptr;
    other.scpiIO_ = nullptr;
}

Lease& Lease::operator=(Lease&& other) noexcept
{
    if (this != &other) {
        // 先释放当前持有的租约
        release();

        resourceId_ = std::move(other.resourceId_);
        mode_ = other.mode_;
        resource_ = other.resource_;
        scpiIO_ = other.scpiIO_;
        leaseId_ = other.leaseId_;
        valid_ = other.valid_;
        releaser_ = std::move(other.releaser_);

        other.valid_ = false;
        other.leaseId_ = -1;
        other.resource_ = nullptr;
        other.scpiIO_ = nullptr;
    }
    return *this;
}

void Lease::release()
{
    if (!valid_) return;
    valid_ = false;
    if (releaser_) {
        releaser_(leaseId_);
    }
    releaser_ = nullptr;
    resource_ = nullptr;
    scpiIO_ = nullptr;
    leaseId_ = -1;
}

// ==================================================================
// ResourceManager 实现
// ==================================================================

ResourceManager::~ResourceManager()
{
    // 强制释放所有资源
    for (auto& [id, entry] : resources_) {
        std::unique_lock lock(entry->entryMutex);
        if (entry->resource && entry->state == ResourceState::Open) {
            entry->resource->close();
            // 触发资源关闭事件
            if (resourceClosedCb_) {
                lock.unlock();
                resourceClosedCb_(id, entry->resource);
                lock.lock();
            }
        }
        entry->state = ResourceState::Closed;
    }
}

bool ResourceManager::registerResource(const std::string& resourceId, IResource* resource)
{
    if (!resource) return false;

    std::lock_guard lock(globalMutex_);
    if (resources_.find(resourceId) != resources_.end()) {
        return false; // 已存在
    }

    auto entry = std::make_unique<ResourceEntry>();
    entry->resource = resource;
    entry->scpiIO = nullptr;
    entry->state = ResourceState::Closed;
    resources_[resourceId] = std::move(entry);
    return true;
}

bool ResourceManager::registerScpiResource(const std::string& resourceId,
                                            IResource* resource, IScpiIO* scpiIO)
{
    if (!resource || !scpiIO) return false;

    std::lock_guard lock(globalMutex_);
    if (resources_.find(resourceId) != resources_.end()) {
        return false;
    }

    auto entry = std::make_unique<ResourceEntry>();
    entry->resource = resource;
    entry->scpiIO = scpiIO;
    entry->state = ResourceState::Closed;
    resources_[resourceId] = std::move(entry);
    return true;
}

bool ResourceManager::registerIoResource(const std::string& resourceId, IScpiIO* io)
{
    if (!io) return false;

    std::lock_guard lock(globalMutex_);
    if (resources_.find(resourceId) != resources_.end()) {
        return false;
    }

    // 创建 IoResourceProxy 包装 IScpiIO 为 IResource
    auto proxy = std::make_unique<IoResourceProxy>(QString::fromStdString(resourceId), io);

    auto entry = std::make_unique<ResourceEntry>();
    entry->resource = proxy.get();
    entry->scpiIO = io;
    entry->state = ResourceState::Open; // 假设已连接
    // 注意：proxy 的生命周期需要管理好 — 暂时由调用方保证 io 的生存期
    resources_[resourceId] = std::move(entry);
    return true;
}

bool ResourceManager::unregisterResource(const std::string& resourceId)
{
    std::lock_guard lock(globalMutex_);
    auto it = resources_.find(resourceId);
    if (it == resources_.end()) return false;

    auto& entry = it->second;
    std::lock_guard entryLock(entry->entryMutex);
    if (entry->sharedCount > 0 || entry->hasExclusiveOwner) {
        return false; // 仍有活跃租约
    }
    if (entry->resource && entry->state == ResourceState::Open) {
        entry->resource->close();
        if (resourceClosedCb_) {
            resourceClosedCb_(resourceId, entry->resource);
        }
    }
    resources_.erase(it);
    return true;
}

std::unique_ptr<Lease> ResourceManager::acquire(const std::string& resourceId,
                                                  LeaseMode mode,
                                                  std::chrono::milliseconds timeout)
{
    // 1. 查找资源条目
    ResourceEntry* entry = nullptr;
    {
        std::lock_guard lock(globalMutex_);
        auto it = resources_.find(resourceId);
        if (it == resources_.end()) return nullptr;
        entry = it->second.get();
    }

    // 2. 获取 per-entry 锁
    std::unique_lock lock(entry->entryMutex);

    // 3. 确保资源已打开
    if (entry->state == ResourceState::Closed) {
        entry->state = ResourceState::Opening;
        lock.unlock(); // 避免 open() 持锁阻塞其他人查状态

        bool openOk = entry->resource && entry->resource->open();

        lock.lock();
        if (!openOk) {
            entry->state = ResourceState::Closed;
            // 通知所有等待者
            entry->cv.notify_all();
            return nullptr;
        }
        entry->state = ResourceState::Open;
        // 触发资源打开事件（参考 OpenTAP ResourceOpened）
        if (resourceOpenedCb_) {
            lock.unlock();
            resourceOpenedCb_(resourceId, entry->resource);
            lock.lock();
        }
        entry->cv.notify_all();
    }

    // 4. 等待直到满足获取条件
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool acquired = false;
    bool waited = false;

    while (true) {
        if (mode == LeaseMode::Exclusive) {
            if (entry->sharedCount == 0 && !entry->hasExclusiveOwner) {
                // 可以获取独占租约
                entry->hasExclusiveOwner = true;
                acquired = true;
                // 记录持有者
                {
                    std::lock_guard dl(deadlockMutex_);
                    ownerMap_[resourceId] = std::this_thread::get_id();
                    waitMap_.erase(std::this_thread::get_id());
                }
                break;
            }
        } else { // Shared
            if (!entry->hasExclusiveOwner) {
                // 没有独占持有者，可以获取共享租约
                entry->sharedCount++;
                acquired = true;
                break;
            }
        }

        // 等待条件变量
        if (timeout.count() <= 0) {
            // 不等待就退出 — 清除等待记录
            {
                std::lock_guard dl(deadlockMutex_);
                waitMap_.erase(std::this_thread::get_id());
            }
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            // 超时退出 — 清除等待记录
            {
                std::lock_guard dl(deadlockMutex_);
                waitMap_.erase(std::this_thread::get_id());
            }
            break;
        }

        if (!waited) {
            waited = true;
            // 记录此线程正在等待此资源（wait-for graph 的边）
            {
                std::lock_guard dl(deadlockMutex_);
                waitMap_[std::this_thread::get_id()] = resourceId;
            }
        }

        entry->cv.wait_until(lock, deadline);
    }

    if (!acquired) return nullptr;

    // 5. 记录 lease -> resourceId 映射
    int leaseId = nextLeaseId_++;
    entry->currentLeaseId = leaseId;
    {
        std::lock_guard lk(leaseMapMutex_);
        leaseMap_[leaseId] = resourceId;
    }
    activeLeaseCount_++;

    // 注册心跳信息
    {
        std::lock_guard lk(heartbeatMutex_);
        LeaseHeartbeat hb;
        hb.leaseId = leaseId;
        hb.resourceId = resourceId;
        hb.lastHeartbeat = std::chrono::steady_clock::now();
        hb.ttl = std::chrono::milliseconds(10000);
        heartbeats_[leaseId] = std::move(hb);
    }

    // 触发租约获取事件
    if (leaseAcquiredCb_) {
        leaseAcquiredCb_(resourceId, leaseId, mode);
    }

    return std::unique_ptr<Lease>(new Lease(
        resourceId, mode, entry->resource, entry->scpiIO,
        leaseId, [this](int id) { releaseLease(id); }));
}

bool ResourceManager::preallocate(const std::string& resourceId,
                                   LeaseMode mode,
                                   std::chrono::milliseconds timeout)
{
    // preallocate ≈ acquire + 立即释放，但保留资源 open 状态
    auto lease = acquire(resourceId, mode, timeout);
    return lease != nullptr;
    // lease 析构时释放，但资源保持 Open（Release 不 Close 资源）
}

int ResourceManager::preallocateAll(const std::vector<std::string>& resourceIds,
                                     LeaseMode mode,
                                     std::chrono::milliseconds perResourceTimeout)
{
    if (resourceIds.empty()) return 0;

    // 筛选已注册的资源
    std::vector<std::string> validIds;
    {
        std::lock_guard lock(globalMutex_);
        for (const auto& rid : resourceIds) {
            if (resources_.find(rid) != resources_.end()) {
                validIds.push_back(rid);
            }
        }
    }

    if (validIds.empty()) return 0;

    // 并行打开所有资源（参考 OpenTAP ResourceTaskManager.OpenResource 模式）
    std::vector<std::future<bool>> futures;
    futures.reserve(validIds.size());

    for (const auto& rid : validIds) {
        futures.push_back(std::async(std::launch::async, [this, rid, mode, perResourceTimeout]() {
            return preallocate(rid, mode, perResourceTimeout);
        }));
    }

    // 等待所有打开操作完成
    int successCount = 0;
    for (size_t i = 0; i < futures.size(); ++i) {
        try {
            if (futures[i].get()) {
                successCount++;
            }
        } catch (const std::exception& e) {
            // 单个资源打开失败不影响其他资源
        }
    }

    return successCount;
}

std::vector<std::string> ResourceManager::registeredResources() const
{
    std::lock_guard lock(globalMutex_);
    std::vector<std::string> ids;
    ids.reserve(resources_.size());
    for (const auto& [id, _] : resources_) {
        ids.push_back(id);
    }
    return ids;
}

std::string ResourceManager::resourceState(const std::string& resourceId) const
{
    std::lock_guard lock(globalMutex_);
    auto it = resources_.find(resourceId);
    if (it == resources_.end()) return "not_registered";

    auto& entry = it->second;
    std::lock_guard entryLock(entry->entryMutex);
    switch (entry->state) {
    case ResourceState::Closed:   return "closed";
    case ResourceState::Opening:  return "opening";
    case ResourceState::Open:     return "open";
    case ResourceState::Closing:  return "closing";
    default:                      return "unknown";
    }
}

void ResourceManager::releaseLease(int leaseId)
{
    // 通过 leaseMap 精确定位 resourceId
    std::string resourceId;
    {
        std::lock_guard lk(leaseMapMutex_);
        auto it = leaseMap_.find(leaseId);
        if (it == leaseMap_.end()) return;
        resourceId = it->second;
        leaseMap_.erase(it);
    }

    // 找到对应的 entry 并释放
    {
        std::lock_guard lock(globalMutex_);
        auto it = resources_.find(resourceId);
        if (it == resources_.end()) return;

        auto& entry = it->second;
        std::unique_lock entryLock(entry->entryMutex);
        LeaseMode mode = entry->hasExclusiveOwner ? LeaseMode::Exclusive : LeaseMode::Shared;
        if (entry->hasExclusiveOwner) {
            entry->hasExclusiveOwner = false;
            // 清除持有者记录
            {
                std::lock_guard dl(deadlockMutex_);
                ownerMap_.erase(resourceId);
            }
        } else if (entry->sharedCount > 0) {
            entry->sharedCount--;
        }
        activeLeaseCount_--;

        // 触发租约释放事件
        if (leaseReleasedCb_) {
            entryLock.unlock();
            leaseReleasedCb_(resourceId, leaseId, mode);
            entryLock.lock();
        }

        entry->cv.notify_all();
    }
}

// ============================================================
// 心跳 / Keepalive（章节 16.1）
// ============================================================

bool ResourceManager::renewLease(int leaseId, std::chrono::milliseconds ttl)
{
    std::lock_guard lk(heartbeatMutex_);
    auto it = heartbeats_.find(leaseId);
    if (it == heartbeats_.end()) return false;
    it->second.lastHeartbeat = std::chrono::steady_clock::now();
    it->second.ttl = ttl;
    return true;
}

void ResourceManager::startHeartbeatMonitor(std::chrono::milliseconds interval,
                                              std::chrono::milliseconds gracePeriod)
{
    if (heartbeatRunning_) return;
    heartbeatRunning_ = true;

    heartbeatThread_ = std::make_unique<std::thread>([this, interval, gracePeriod]() {
        while (heartbeatRunning_) {
            std::this_thread::sleep_for(interval);

            // 死锁检测
            detectDeadlock();

            auto now = std::chrono::steady_clock::now();
            std::vector<int> expiredLeases;

            {
                std::lock_guard lk(heartbeatMutex_);
                for (const auto& [lid, hb] : heartbeats_) {
                    if (now - hb.lastHeartbeat > hb.ttl + gracePeriod) {
                        expiredLeases.push_back(lid);
                    }
                }
            }

            // 回收过期租约（真正 Close 资源 + 触发事件）
            for (int lid : expiredLeases) {
                std::string expiredRid;
                {
                    std::lock_guard lk(heartbeatMutex_);
                    auto hbIt = heartbeats_.find(lid);
                    if (hbIt != heartbeats_.end()) {
                        expiredRid = hbIt->second.resourceId;
                    }
                }

                // 先释放租约
                releaseLease(lid);

                // 强制 Close 资源
                if (!expiredRid.empty()) {
                    std::lock_guard lock(globalMutex_);
                    auto resIt = resources_.find(expiredRid);
                    if (resIt != resources_.end()) {
                        auto& entry = resIt->second;
                        std::lock_guard eLock(entry->entryMutex);
                        if (entry->resource && entry->state == ResourceState::Open) {
                            entry->resource->close();
                            entry->state = ResourceState::Closed;
                            if (resourceClosedCb_) {
                                resourceClosedCb_(expiredRid, entry->resource);
                            }
                        }
                    }
                }

                {
                    std::lock_guard lk(heartbeatMutex_);
                    heartbeats_.erase(lid);
                }
            }
        }
    });
}

void ResourceManager::stopHeartbeatMonitor()
{
    heartbeatRunning_ = false;
    if (heartbeatThread_ && heartbeatThread_->joinable()) {
        heartbeatThread_->join();
    }
    heartbeatThread_.reset();
}

// ============================================================
// 死锁检测（章节 16.2）
// wait-for graph 环检测：DFS 遍历 owner→waiter 链
// ============================================================
void ResourceManager::detectDeadlock()
{
    std::lock_guard dl(deadlockMutex_);
    if (ownerMap_.empty() || waitMap_.empty()) return;

    // 构建 wait-for 图：resource -> thread 的依赖链
    // 对每个等待者线程，检查它等待的资源是否被另一个线程持有，
    // 而那个线程又在等待别的资源，形成环
    for (const auto& [waiterTid, resourceId] : waitMap_) {
        // 这条线程在等待 resourceId
        // 检查谁持有 resourceId
        auto ownerIt = ownerMap_.find(resourceId);
        if (ownerIt == ownerMap_.end()) continue;

        std::thread::id currentTid = ownerIt->second; // 资源持有者线程
        if (currentTid == waiterTid) continue; // 自等？不应该

        // DFS 沿着等待链走：currentTid 在等什么？
        // 限制最大深度，避免无限循环
        std::thread::id prevTid = waiterTid;
        std::string chain = std::to_string(std::hash<std::thread::id>{}(waiterTid));
        int depth = 0;
        constexpr int kMaxDepth = 32;

        while (depth < kMaxDepth) {
            auto waitIt = waitMap_.find(currentTid);
            if (waitIt == waitMap_.end()) break; // currentTid 没在等任何资源 → 无环

            std::string waitingFor = waitIt->second;
            chain += " -> " + waitingFor + "(" + std::to_string(std::hash<std::thread::id>{}(currentTid)) + ")";

            // 检查 waitingFor 被谁持有
            auto ownIt = ownerMap_.find(waitingFor);
            if (ownIt == ownerMap_.end()) break;

            if (ownIt->second == waiterTid) {
                // **发现环！** waitingFor 的持有者就是最初等待的线程
                qWarning().noquote()
                    << QString("[Deadlock] Circular wait detected: %1").arg(QString::fromStdString(chain));

                if (preemptionPolicy_ == PreemptionPolicy::ForcePreempt) {
                    // 强制回收：释放当前线程持有的所有资源
                    // 简化处理：移除等待记录
                    waitMap_.erase(currentTid);
                    qWarning().noquote() << "[Deadlock] Preempted thread" << QString::number(std::hash<std::thread::id>{}(currentTid));
                }
                break;
            }

            prevTid = currentTid;
            currentTid = ownIt->second;
            depth++;
        }
    }
}

} // namespace eon::sdk
