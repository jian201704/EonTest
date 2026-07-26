#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace eon::sdk {

class IResource;
class IScpiIO;

/// 资源租赁模式
enum class LeaseMode {
    Shared,     ///< 共享模式：多个请求者可同时持有
    Exclusive   ///< 独占模式：仅一个请求者可持有
};

/// 资源租约 — RAII 风格，析构时自动释放
class Lease {
public:
    Lease() = default;
    ~Lease();

    Lease(Lease&&) noexcept;
    Lease& operator=(Lease&&) noexcept;

    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    /// 显式释放租约
    void release();

    /// 资源 ID
    const std::string& resourceId() const { return resourceId_; }

    /// 租赁模式
    LeaseMode mode() const { return mode_; }

    /// 是否持有有效租约
    bool isValid() const { return valid_; }

    /// 获取底层资源接口
    IResource* resource() const { return resource_; }

    /// 获取 SCPI IO 接口（如果资源支持）
    IScpiIO* scpiIO() const { return scpiIO_; }

private:
    friend class ResourceManager;

    Lease(const std::string& resourceId, LeaseMode mode,
          IResource* resource, IScpiIO* scpiIO,
          int leaseId, std::function<void(int)> releaser);

    std::string resourceId_;
    LeaseMode mode_ = LeaseMode::Exclusive;
    IResource* resource_ = nullptr;
    IScpiIO* scpiIO_ = nullptr;
    int leaseId_ = -1;
    bool valid_ = false;
    std::function<void(int)> releaser_;
};

/// 资源管理器 — 引擎级统一管理 IResource 生命周期
///
/// 职责：
/// - 注册/注销资源
/// - 提供 Acquire（获取租约）/ Release（释放租约）
/// - 支持独占/共享模式
/// - 线程安全 per-entry 锁
/// - 超时回收
///
/// 使用示例：
///   auto lease = resMgr.acquire("VISA::GPIB::1", LeaseMode::Exclusive, 5s);
///   if (lease) { /* use lease->resource() */ lease->release(); }
///
class ResourceManager {
public:
    ResourceManager() = default;
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    /// 注册资源。resource 的生命周期由调用方管理，ResourceManager 不拥有所有权。
    /// 若 resourceId 已存在则返回 false。
    bool registerResource(const std::string& resourceId, IResource* resource);

    /// 注册 SCPI 资源（同时持有 IResource 和 IScpiIO 引用）
    bool registerScpiResource(const std::string& resourceId,
                               IResource* resource, IScpiIO* scpiIO);

    /// 注册纯 IO 资源（无 IResource 包装，用于 ScpiStepPlugin 等直接管理连接的场景）
    /// ResourceManager 内部会创建一个代理 IResource 来管理 open/close。
    bool registerIoResource(const std::string& resourceId, IScpiIO* io);

    /// 取消注册资源。若该资源当前有活跃租约则返回 false。
    bool unregisterResource(const std::string& resourceId);

    /// 获取资源租约。超时后返回 nullptr。
    /// @param resourceId 资源标识
    /// @param mode 独占/共享
    /// @param timeout 等待超时
    std::unique_ptr<Lease> acquire(const std::string& resourceId,
                                    LeaseMode mode,
                                    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /// 预分配资源（不返回 Lease，但确保资源已 Open 可用）
    bool preallocate(const std::string& resourceId, LeaseMode mode,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /// 批量异步预分配（参考 OpenTAP 并行打开模式）
    /// 所有资源并行打开，每个资源独立超时
    /// @return 成功预分配的资源数
    int preallocateAll(const std::vector<std::string>& resourceIds, LeaseMode mode,
                       std::chrono::milliseconds perResourceTimeout = std::chrono::milliseconds(5000));

    /// 获取已注册的资源列表
    std::vector<std::string> registeredResources() const;

    /// 获取资源状态描述
    std::string resourceState(const std::string& resourceId) const;

    /// 当前活跃租约数
    int activeLeaseCount() const { return activeLeaseCount_.load(); }

    // ============================================================
    // 事件回调（参考 OpenTAP IResourceManager.ResourceOpened）
    // ============================================================

    /// 资源打开时触发（参数：resourceId, IResource*）
    using ResourceEvent = std::function<void(const std::string&, IResource*)>;

    /// 注册资源打开回调
    void onResourceOpened(ResourceEvent cb) { resourceOpenedCb_ = std::move(cb); }

    /// 注册资源关闭回调
    void onResourceClosed(ResourceEvent cb) { resourceClosedCb_ = std::move(cb); }

    /// 注册租约获取回调（参数：resourceId, leaseId, mode）
    using LeaseEvent = std::function<void(const std::string&, int, LeaseMode)>;
    void onLeaseAcquired(LeaseEvent cb) { leaseAcquiredCb_ = std::move(cb); }
    void onLeaseReleased(LeaseEvent cb) { leaseReleasedCb_ = std::move(cb); }

    // ============================================================
    // 心跳 / Keepalive（章节 16.1）
    // ============================================================

    /// 续约 — Agent 定期调用以保持租约有效
    bool renewLease(int leaseId, std::chrono::milliseconds ttl = std::chrono::milliseconds(10000));

    /// 启动心跳监控线程（自动回收过期租约）
    void startHeartbeatMonitor(std::chrono::milliseconds interval = std::chrono::milliseconds(1000),
                                std::chrono::milliseconds gracePeriod = std::chrono::milliseconds(3000));

    /// 停止心跳监控线程
    void stopHeartbeatMonitor();

    /// 配置抢占策略（章节 16.2）
    enum class PreemptionPolicy { None, Notify, ForcePreempt, PriorityBased };
    void setPreemptionPolicy(PreemptionPolicy policy) { preemptionPolicy_ = policy; }

private:
    friend class Lease;

    enum class ResourceState { Closed, Opening, Open, Closing };

    struct ResourceEntry {
        IResource* resource = nullptr;
        IScpiIO* scpiIO = nullptr;
        ResourceState state = ResourceState::Closed;
        int sharedCount = 0;
        bool hasExclusiveOwner = false;
        int currentLeaseId = 0;
        mutable std::mutex entryMutex;
        std::condition_variable cv;
    };

    void releaseLease(int leaseId);

    // 主表：resourceId -> ResourceEntry
    std::unordered_map<std::string, std::unique_ptr<ResourceEntry>> resources_;
    mutable std::mutex globalMutex_;   // 仅保护 resources_ 表的插入/删除

    // 事件回调
    ResourceEvent resourceOpenedCb_;
    ResourceEvent resourceClosedCb_;
    LeaseEvent leaseAcquiredCb_;
    LeaseEvent leaseReleasedCb_;

    // 心跳 / Keepalive
    struct LeaseHeartbeat {
        int leaseId = -1;
        std::string resourceId;
        std::chrono::steady_clock::time_point lastHeartbeat;
        std::chrono::milliseconds ttl{10000};
    };
    std::unordered_map<int, LeaseHeartbeat> heartbeats_;
    mutable std::mutex heartbeatMutex_;
    std::unique_ptr<std::thread> heartbeatThread_;
    std::atomic<bool> heartbeatRunning_{false};
    PreemptionPolicy preemptionPolicy_ = PreemptionPolicy::None;

    // 死锁检测（章节 16.2）：wait-for graph
    // ownerMap_: resourceId -> threadId（持有者）
    std::unordered_map<std::string, std::thread::id> ownerMap_;
    // waitMap_: threadId -> resourceId（等待者）
    std::unordered_map<std::thread::id, std::string> waitMap_;
    mutable std::mutex deadlockMutex_;

    /// 检测 wait-for graph 中的环
    void detectDeadlock();

    // leaseId -> resourceId 映射，用于 releaseLease 精确定位
    std::unordered_map<int, std::string> leaseMap_;
    mutable std::mutex leaseMapMutex_;

    std::atomic<int> nextLeaseId_{ 1 };
    std::atomic<int> activeLeaseCount_{ 0 };
};

} // namespace eon::sdk
