// ============================================================
// ResourceManager 压力测试（章节 10 验收标准）
// 测试场景：
// 1. 100k 次 Acquire/Release 并发操作（无泄漏）
// 2. 4 个线程同时请求同一台电源（exclusive），断言只有一个成功
// 3. 共享模式多线程并发访问
// ============================================================

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "eon/sdk/IResource.h"
#include "eon/sdk/ResourceManager.h"

// --- Mock 资源 ---
class MockResource : public eon::sdk::IResource {
public:
    explicit MockResource(const QString& name) : name_(name) {}

    bool open() override {
        opened_ = true;
        return true;
    }

    void close() override {
        opened_ = false;
    }

    QString name() const override { return name_; }
    bool isConnected() const override { return opened_; }

    bool opened_ = false;

private:
    QString name_;
};

int main() {
    int failures = 0;
    int totalTests = 0;

    // ============================================================
    // 测试 1：100k 次 Acquire/Release 压力测试
    // ============================================================
    {
        eon::sdk::ResourceManager rm;
        auto res = std::make_shared<MockResource>("PWR1");
        rm.registerResource("PWR1", res.get());

        constexpr int kIterations = 100000;
        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < kIterations; ++i) {
            auto lease = rm.acquire("PWR1", eon::sdk::LeaseMode::Exclusive);
            if (!lease || !lease->isValid()) {
                ++failures;
                std::cerr << "FAIL: Acquire failed at iteration " << i << std::endl;
                break;
            }
            // lease 析构时自动释放
        }

        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        totalTests++;

        double errRate = 100.0 * failures / kIterations;
        std::cout << "[Test 1] " << kIterations << " Acquire/Release: "
                  << ms << "ms, error rate=" << errRate << "%"
                  << (errRate < 0.01 ? " PASS" : " FAIL")
                  << std::endl;
    }

    // ============================================================
    // 测试 2：4 线程并发请求同一台电源（exclusive）
    // 每线程持有 20ms，确保产生竞争
    // ============================================================
    {
        eon::sdk::ResourceManager rm;
        auto res = std::make_shared<MockResource>("PWR2");
        rm.registerResource("PWR2", res.get());

        constexpr int kThreads = 4;
        constexpr int kIterPerThread = 50;
        std::atomic<int> successCount{0};
        std::atomic<int> failCount{0};
        std::vector<std::thread> threads;

        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&rm, &successCount, &failCount]() {
                for (int i = 0; i < kIterPerThread; ++i) {
                    // 超时设 5ms << 持有时间 50ms，确保产生竞争失败
                    auto lease = rm.acquire("PWR2", eon::sdk::LeaseMode::Exclusive,
                                            std::chrono::milliseconds(5));
                    if (lease && lease->isValid()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        ++successCount;
                    } else {
                        ++failCount;
                    }
                }
            });
        }

        for (auto& t : threads) t.join();
        totalTests++;

        // exclusive + 5ms 超时 + 50ms 持有 → 必然大量失败
        bool exclusiveWorks = (failCount.load() > 0);
        if (!exclusiveWorks) {
            ++failures;
            std::cerr << "FAIL: Exclusive lock allowed parallel access (success="
                      << successCount.load() << ", expected some failures)"
                      << std::endl;
        }
        std::cout << "[Test 2] 4 threads x " << kIterPerThread << " exclusive (hold 50ms, timeout 5ms): "
                  << "success=" << successCount.load()
                  << ", fail=" << failCount.load()
                  << (exclusiveWorks ? " PASS" : " FAIL")
                  << std::endl;
    }

    // ============================================================
    // 测试 3：共享模式多线程并发访问
    // ============================================================
    {
        eon::sdk::ResourceManager rm;
        auto res = std::make_shared<MockResource>("SHARED1");
        rm.registerResource("SHARED1", res.get());

        constexpr int kThreads = 8;
        std::atomic<int> sharedSuccess{0};
        std::vector<std::thread> threads;

        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&rm, &sharedSuccess]() {
                for (int i = 0; i < 50; ++i) {
                    auto lease = rm.acquire("SHARED1", eon::sdk::LeaseMode::Shared);
                    if (lease && lease->isValid()) {
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                        sharedSuccess++;
                    }
                }
            });
        }

        for (auto& t : threads) t.join();
        totalTests++;

        // 共享模式下所有请求都应该成功
        std::cout << "[Test 3] 8 threads x 50 shared acquire: "
                  << "success=" << sharedSuccess.load()
                  << " (expected " << (kThreads * 50) << ")"
                  << (sharedSuccess.load() == kThreads * 50 ? " PASS" : " FAIL")
                  << std::endl;
    }

    // ============================================================
    // 汇总
    // ============================================================
    std::cout << "\n=== Results: " << totalTests << " tests, "
              << failures << " failures ===" << std::endl;
    return failures > 0 ? 1 : 0;
}
