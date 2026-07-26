#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <QJsonObject>
#include <QString>

namespace eon::sdk {

/// <summary>
/// Prometheus 遥测导出器（P2）。
/// 暴露指标供 Prometheus 抓取，配合 Grafana 做可视化。
///
/// 指标格式：
///   # HELP eon_workflows_started Total workflows started
///   # TYPE eon_workflows_started counter
///   eon_workflows_started{cell="cell-01"} 42
///
/// 使用方式：
///   启动 HTTP 端点用于 Prometheus 拉取：
///   exporter.startHttpEndpoint(9100);
/// </summary>
class TelemetryExporter {
public:
    TelemetryExporter() = default;
    ~TelemetryExporter();

    TelemetryExporter(const TelemetryExporter&) = delete;
    TelemetryExporter& operator=(const TelemetryExporter&) = delete;

    // ============================================================
    // 计数器
    // ============================================================

    void incrementWorkflowsStarted(const QString& cellId = {});
    void incrementWorkflowsFinished(const QString& cellId = {});
    void incrementWorkflowsFailed(const QString& cellId = {});
    void incrementStepsStarted(const QString& cellId = {});
    void incrementStepsFinished(const QString& cellId = {});
    void incrementStepsFailed(const QString& cellId = {});
    void incrementRetries(const QString& cellId = {});
    void observeStepDuration(double seconds, const QString& pluginId = {});

    // ============================================================
    // 指标查询
    // ============================================================

    /// 生成 Prometheus 文本格式的所有指标
    QString scrape() const;

    /// 重置所有计数器
    void reset();

    // ============================================================
    // HTTP 端点（用于 Prometheus 拉取）
    // ============================================================

    /// 启动内嵌 HTTP 服务器暴露 /metrics 端点
    /// @param port HTTP 端口号，默认 9100
    bool startHttpEndpoint(int port = 9100);

    /// 停止 HTTP 服务器
    void stopHttpEndpoint();

private:
    struct Counter {
        std::atomic<long long> value{0};
        mutable std::mutex mtx;
    };

    struct Histogram {
        mutable std::mutex mtx;
        long long count = 0;
        double sum = 0.0;
        double min = 0.0;
        double max = 0.0;
    };

    std::unordered_map<std::string, Counter> workflowsStarted_;
    std::unordered_map<std::string, Counter> workflowsFinished_;
    std::unordered_map<std::string, Counter> workflowsFailed_;
    std::unordered_map<std::string, Counter> stepsStarted_;
    std::unordered_map<std::string, Counter> stepsFinished_;
    std::unordered_map<std::string, Counter> stepsFailed_;
    std::unordered_map<std::string, Counter> retries_;
    std::unordered_map<std::string, Histogram> stepDurations_;

    mutable std::mutex globalMutex_;

    // HTTP 服务线程
    std::unique_ptr<std::thread> httpThread_;
    std::atomic<bool> httpRunning_{false};
    int httpPort_ = 0;

    void httpServerLoop();
    QString handleRequest(const QString& request);
};

} // namespace eon::sdk
