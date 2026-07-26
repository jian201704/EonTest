#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eon::sdk {

class ResourceManager;

/// <summary>
/// 路由租约 — RAII 风格，析构时自动断开路由。
/// 对应章节 7 的 RouteLease 概念。
/// </summary>
class RouteLease {
public:
    RouteLease() = default;
    ~RouteLease();

    RouteLease(RouteLease&&) noexcept;
    RouteLease& operator=(RouteLease&&) noexcept;
    RouteLease(const RouteLease&) = delete;
    RouteLease& operator=(const RouteLease&) = delete;

    /// 显式断开路由
    void disconnect();

    bool isValid() const { return valid_; }
    std::string src() const { return src_; }
    std::string dst() const { return dst_; }

    /// 构造 RouteLease（由 MatrixManager 创建，不推荐外部直接调用）
    RouteLease(std::string src, std::string dst,
               std::function<void()> disconnectFn);

private:
    friend class MatrixManager;

    std::string src_;
    std::string dst_;
    bool valid_ = false;
    std::function<void()> disconnectFn_;
};

/// <summary>
/// 矩阵/路由管理器（章节 7）。
/// 用于多 CELL 测试中通过交换矩阵共享仪器。
///
/// 职责：
/// - 申请路由：RouteTo(src, dst) 返回 RouteLease
/// - 批量路由事务：BatchRoute 在一个原子操作中设置多个连接
/// - 集成 ResourceManager：路由请求通过 RM 获取矩阵硬件独占控制
/// </summary>
class MatrixManager {
public:
    explicit MatrixManager(ResourceManager* rm = nullptr);
    ~MatrixManager();

    MatrixManager(const MatrixManager&) = delete;
    MatrixManager& operator=(const MatrixManager&) = delete;

    /// 设置底层矩阵硬件资源 ID（用于通过 ResourceManager 获取矩阵控制权）
    void setMatrixResourceId(const std::string& resourceId) { matrixResourceId_ = resourceId; }

    /// 申请路由：src -> dst
    /// 返回 RouteLease，析构时自动断开
    std::unique_ptr<RouteLease> routeTo(const std::string& src, const std::string& dst,
                                         std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /// 批量路由事务：在一个原子操作中设置多个连接
    /// 所有连接要么全部成功，要么全部回滚
    struct RouteEntry { std::string src; std::string dst; };
    std::vector<std::unique_ptr<RouteLease>> batchRoute(
        const std::vector<RouteEntry>& routes,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(10000));

    /// 断开所有路由
    void disconnectAll();

    /// 获取当前活跃路由数
    int activeRouteCount() const { return activeCount_.load(); }

    /// 获取当前路由拓扑（src -> dst 映射）
    std::unordered_map<std::string, std::string> currentTopology() const;

    /// 验证路由是否可用（检查目标是否可达）
    bool verifyRoute(const std::string& src, const std::string& dst,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

private:
    friend class RouteLease;
    void doDisconnect(const std::string& src, const std::string& dst);

    ResourceManager* resourceManager_ = nullptr;
    std::string matrixResourceId_;

    // 活跃路由表: src -> dst
    std::unordered_map<std::string, std::string> routes_;
    mutable std::mutex routesMutex_;
    std::atomic<int> activeCount_{0};
};

} // namespace eon::sdk
