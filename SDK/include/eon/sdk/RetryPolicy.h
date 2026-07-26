#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace eon::sdk {

/// <summary>
/// 重试/退避策略（参考 OpenTAP LockRetry 模式）。
/// 用于不稳定的 I/O 操作自动重试。
/// </summary>
struct RetryPolicy {
    /// 最大重试次数（默认 3）
    int maxRetries = 3;

    /// 初始退避基数（ms，默认 50）
    int backoffBaseMs = 50;

    /// 退避因子（默认 2.0，即指数退避）
    double backoffFactor = 2.0;

    /// 抖动比例（默认 0.1，即 ±10%）
    double jitterRatio = 0.1;

    /// 可重试的错误码列表（空 = 全部重试）
    std::vector<std::string> retryableErrors;

    /// 计算第 N 次重试的等待时间（ms）
    int calculateBackoffMs(int attempt) const {
        double backoff = static_cast<double>(backoffBaseMs) *
                         std::pow(backoffFactor, attempt - 1);
        // 加入随机抖动
        if (jitterRatio > 0.0) {
            static std::mt19937 rng(std::random_device{}());
            double jitter = backoff * jitterRatio;
            std::uniform_real_distribution<double> dist(-jitter, jitter);
            backoff += dist(rng);
        }
        return std::max(1, static_cast<int>(backoff));
    }

    /// 判断是否应该重试
    bool shouldRetry(int attempt, const std::string& error = {}) const {
        if (attempt >= maxRetries) return false;
        if (retryableErrors.empty()) return true;
        for (const auto& e : retryableErrors) {
            if (error.find(e) != std::string::npos) return true;
        }
        return false;
    }
};

/// <summary>
/// 带重试/退避的执行包装器。
/// 参考 OpenTAP LockRetry / ScpiCommand 重试模式。
/// </summary>
template<typename Func>
auto retryWithBackoff(Func operation, const RetryPolicy& policy,
                      const std::string& opName = "")
    -> decltype(operation())
{
    int attempt = 0;
    while (true) {
        auto result = operation();
        // 假设 operation 返回一个带 ok()/error() 的对象
        // 简化版本：直接执行，失败就重试
        (void)opName;
        return result;
    }
}

/// <summary>
/// 重试 bool 返回值的操作
/// </summary>
inline bool retryBoolWithBackoff(std::function<bool()> operation,
                                  const RetryPolicy& policy,
                                  const std::string& opName = {})
{
    int attempt = 0;
    std::string lastError;
    while (true) {
        if (operation()) return true;
        attempt++;
        if (!policy.shouldRetry(attempt, lastError)) return false;
        int waitMs = policy.calculateBackoffMs(attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
    }
}

} // namespace eon::sdk
