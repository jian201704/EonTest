#include "eon/sdk/TelemetryExporter.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QDebug>

namespace eon::sdk {

TelemetryExporter::~TelemetryExporter()
{
    stopHttpEndpoint();
}

// ============================================================
// 计数器
// ============================================================

#define INCREMENT_COUNTER(map, cellId) \
    { std::lock_guard lk(globalMutex_); map[cellId.toStdString()].value++; }

void TelemetryExporter::incrementWorkflowsStarted(const QString& cellId) { INCREMENT_COUNTER(workflowsStarted_, cellId); }
void TelemetryExporter::incrementWorkflowsFinished(const QString& cellId) { INCREMENT_COUNTER(workflowsFinished_, cellId); }
void TelemetryExporter::incrementWorkflowsFailed(const QString& cellId) { INCREMENT_COUNTER(workflowsFailed_, cellId); }
void TelemetryExporter::incrementStepsStarted(const QString& cellId) { INCREMENT_COUNTER(stepsStarted_, cellId); }
void TelemetryExporter::incrementStepsFinished(const QString& cellId) { INCREMENT_COUNTER(stepsFinished_, cellId); }
void TelemetryExporter::incrementStepsFailed(const QString& cellId) { INCREMENT_COUNTER(stepsFailed_, cellId); }
void TelemetryExporter::incrementRetries(const QString& cellId) { INCREMENT_COUNTER(retries_, cellId); }

void TelemetryExporter::observeStepDuration(double seconds, const QString& pluginId)
{
    std::lock_guard lk(globalMutex_);
    auto& h = stepDurations_[pluginId.toStdString()];
    std::lock_guard hl(h.mtx);
    h.count++;
    h.sum += seconds;
    if (h.count == 1 || seconds < h.min) h.min = seconds;
    if (h.count == 1 || seconds > h.max) h.max = seconds;
}

// ============================================================
// Prometheus 文本格式输出
// ============================================================

#define PROMETHEUS_COUNTER(name, help, map) \
    do { \
        out += "# HELP " name " " help "\n"; \
        out += "# TYPE " name " counter\n"; \
        for (const auto& [k, v] : map) { \
            out += name "{cell=\"" + QString::fromStdString(k) + "\"} " + QString::number(v.value.load()) + "\n"; \
        } \
    } while(0)

QString TelemetryExporter::scrape() const
{
    QString out;
    std::lock_guard lk(globalMutex_);

    PROMETHEUS_COUNTER("eon_workflows_started", "Total workflows started", workflowsStarted_);
    PROMETHEUS_COUNTER("eon_workflows_finished", "Total workflows finished", workflowsFinished_);
    PROMETHEUS_COUNTER("eon_workflows_failed", "Total workflows failed", workflowsFailed_);
    PROMETHEUS_COUNTER("eon_steps_started", "Total steps started", stepsStarted_);
    PROMETHEUS_COUNTER("eon_steps_finished", "Total steps finished", stepsFinished_);
    PROMETHEUS_COUNTER("eon_steps_failed", "Total steps failed", stepsFailed_);
    PROMETHEUS_COUNTER("eon_retries_total", "Total retries", retries_);

    // Histogram
    for (const auto& [plugin, h] : stepDurations_) {
        std::lock_guard hl(h.mtx);
        if (h.count == 0) continue;
        out += "# HELP eon_step_duration_seconds Step execution duration\n";
        out += "# TYPE eon_step_duration_seconds gauge\n";
        QString p = QString::fromStdString(plugin);
        out += QString("eon_step_duration_seconds_count{plugin=\"%1\"} %2\n").arg(p).arg(h.count);
        out += QString("eon_step_duration_seconds_sum{plugin=\"%1\"} %2\n").arg(p).arg(h.sum, 0, 'f', 3);
        out += QString("eon_step_duration_seconds_min{plugin=\"%1\"} %2\n").arg(p).arg(h.min, 0, 'f', 3);
        out += QString("eon_step_duration_seconds_max{plugin=\"%1\"} %2\n").arg(p).arg(h.max, 0, 'f', 3);
    }

    return out;
}

void TelemetryExporter::reset()
{
    std::lock_guard lk(globalMutex_);
    workflowsStarted_.clear();
    workflowsFinished_.clear();
    workflowsFailed_.clear();
    stepsStarted_.clear();
    stepsFinished_.clear();
    stepsFailed_.clear();
    retries_.clear();
    stepDurations_.clear();
}

// ============================================================
// HTTP 端点（Prometheus 拉取）
// ============================================================

bool TelemetryExporter::startHttpEndpoint(int port)
{
    if (httpRunning_) return false;

    httpRunning_ = true;
    httpPort_ = port;
    httpThread_ = std::make_unique<std::thread>([this]() { httpServerLoop(); });
    qDebug().noquote() << QString("[Telemetry] Prometheus endpoint started on :%1/metrics").arg(port);
    return true;
}

void TelemetryExporter::stopHttpEndpoint()
{
    httpRunning_ = false;
    if (httpThread_ && httpThread_->joinable()) {
        httpThread_->join();
    }
    httpThread_.reset();
}

void TelemetryExporter::httpServerLoop()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::Any, httpPort_)) {
        qWarning() << "[Telemetry] Failed to start HTTP server on port" << httpPort_;
        httpRunning_ = false;
        return;
    }

    while (httpRunning_) {
        if (!server.waitForNewConnection(1000)) continue;
        auto* socket = server.nextPendingConnection();
        if (!socket) continue;

        // 读取 HTTP 请求
        socket->waitForReadyRead(2000);
        QString request = QString::fromUtf8(socket->readAll());

        // 处理请求
        QString response = handleRequest(request);

        // 发送 HTTP 响应
        QString httpResponse = QString(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Length: %1\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%2"
        ).arg(response.toUtf8().size()).arg(response);

        socket->write(httpResponse.toUtf8());
        socket->flush();
        socket->waitForBytesWritten(1000);
        socket->close();
        delete socket;
    }

    server.close();
}

QString TelemetryExporter::handleRequest(const QString& request)
{
    if (request.startsWith("GET /metrics")) {
        return scrape();
    }
    return "EonTest Telemetry Exporter\nUsage: GET /metrics\n";
}

} // namespace eon::sdk
