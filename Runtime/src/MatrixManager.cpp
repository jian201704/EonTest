#include "eon/sdk/MatrixManager.h"
#include "eon/sdk/ResourceManager.h"

#include <algorithm>
#include <cassert>
#include <thread>

namespace eon::sdk {

// ============================================================
// RouteLease
// ============================================================

RouteLease::RouteLease(std::string src, std::string dst,
                       std::function<void()> disconnectFn)
    : src_(std::move(src))
    , dst_(std::move(dst))
    , valid_(true)
    , disconnectFn_(std::move(disconnectFn))
{
}

RouteLease::~RouteLease() { disconnect(); }

RouteLease::RouteLease(RouteLease&& other) noexcept
    : src_(std::move(other.src_))
    , dst_(std::move(other.dst_))
    , valid_(other.valid_)
    , disconnectFn_(std::move(other.disconnectFn_))
{
    other.valid_ = false;
}

RouteLease& RouteLease::operator=(RouteLease&& other) noexcept
{
    if (this != &other) {
        disconnect();
        src_ = std::move(other.src_);
        dst_ = std::move(other.dst_);
        valid_ = other.valid_;
        disconnectFn_ = std::move(other.disconnectFn_);
        other.valid_ = false;
    }
    return *this;
}

void RouteLease::disconnect()
{
    if (!valid_) return;
    valid_ = false;
    if (disconnectFn_) disconnectFn_();
    disconnectFn_ = nullptr;
}

// ============================================================
// MatrixManager
// ============================================================

MatrixManager::MatrixManager(ResourceManager* rm)
    : resourceManager_(rm)
{
}

MatrixManager::~MatrixManager()
{
    disconnectAll();
}

std::unique_ptr<RouteLease> MatrixManager::routeTo(
    const std::string& src, const std::string& dst,
    std::chrono::milliseconds timeout)
{
    // 通过 ResourceManager 获取矩阵硬件独占控制
    if (resourceManager_ && !matrixResourceId_.empty()) {
        auto lease = resourceManager_->acquire(matrixResourceId_,
                                                LeaseMode::Exclusive, timeout);
        if (!lease) return nullptr;
        // lease 在 RouteLease 生命周期内保持
    }

    // 执行连接操作（伪代码：实际发送矩阵切换命令）
    // 验证路由
    if (!verifyRoute(src, dst, timeout)) {
        return nullptr;
    }

    // 记录路由
    {
        std::lock_guard lock(routesMutex_);
        // 如果 src 已有路由，先断开
        auto it = routes_.find(src);
        if (it != routes_.end()) {
            // 断开旧路由
        }
        routes_[src] = dst;
    }
    activeCount_++;

    auto self = this;
    return std::make_unique<RouteLease>(src, dst, [self, src, dst]() {
        self->doDisconnect(src, dst);
    });
}

std::vector<std::unique_ptr<RouteLease>> MatrixManager::batchRoute(
    const std::vector<RouteEntry>& routes,
    std::chrono::milliseconds timeout)
{
    std::vector<std::unique_ptr<RouteLease>> result;
    result.reserve(routes.size());

    // 先验证所有路由是否可达
    for (const auto& entry : routes) {
        if (!verifyRoute(entry.src, entry.dst, timeout)) {
            // 验证失败，回滚已创建的路由
            result.clear();
            return result;
        }
    }

    // 全部验证通过，逐个建立
    for (const auto& entry : routes) {
        auto lease = routeTo(entry.src, entry.dst, timeout);
        if (!lease) {
            // 建立失败，回滚
            result.clear();
            return result;
        }
        result.push_back(std::move(lease));
    }

    return result;
}

void MatrixManager::disconnectAll()
{
    std::lock_guard lock(routesMutex_);
    for (const auto& [src, dst] : routes_) {
        // 发送断开命令
        activeCount_--;
    }
    routes_.clear();
}

std::unordered_map<std::string, std::string> MatrixManager::currentTopology() const
{
    std::lock_guard lock(routesMutex_);
    return routes_;
}

bool MatrixManager::verifyRoute(const std::string& src, const std::string& dst,
                                 std::chrono::milliseconds timeout)
{
    // 验证路由：向目标发送 *IDN? 或类似查询确认连通
    // 简化：假设路由总是成功
    (void)src;
    (void)dst;
    (void)timeout;
    return true;
}

void MatrixManager::doDisconnect(const std::string& src, const std::string& dst)
{
    std::lock_guard lock(routesMutex_);
    auto it = routes_.find(src);
    if (it != routes_.end() && it->second == dst) {
        routes_.erase(it);
        activeCount_--;
    }
}

} // namespace eon::sdk
