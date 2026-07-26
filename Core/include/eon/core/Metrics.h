#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace eon::core {

// ============================================================================
// MetricsCollector — 轻量级指标收集器
//
// 支持 Counter / Gauge / Histogram 三种指标类型
// 提供 Prometheus 格式导出
// ============================================================================
class MetricsCollector {
public:
    static MetricsCollector& instance() { static MetricsCollector m; return m; }

    // --- Counter (单调递增) ---
    void incCounter(const QString& name, int delta = 1) {
        QMutexLocker lock(&mutex_);
        counters_[name] = counters_.value(name, 0) + delta;
    }
    int counterValue(const QString& name) const {
        QMutexLocker lock(&mutex_);
        return counters_.value(name, 0);
    }

    // --- Gauge (可增可减) ---
    void setGauge(const QString& name, double value) {
        QMutexLocker lock(&mutex_);
        gauges_[name] = value;
    }
    void incGauge(const QString& name, double delta) {
        QMutexLocker lock(&mutex_);
        gauges_[name] = gauges_.value(name, 0.0) + delta;
    }
    double gaugeValue(const QString& name) const {
        QMutexLocker lock(&mutex_);
        return gauges_.value(name, 0.0);
    }

    // --- Histogram (分桶统计) ---
    void observe(const QString& name, double value) {
        QMutexLocker lock(&mutex_);
        auto& h = histograms_[name];
        h.values.append(value);
        h.sum += value;
        h.count++;
        if (h.count == 1) { h.min = value; h.max = value; }
        else { h.min = std::min(h.min, value); h.max = std::max(h.max, value); }
    }

    // --- 快照 ---
    QVariantMap snapshot() const {
        QMutexLocker lock(&mutex_);
        QVariantMap snap;
        QVariantMap cmap;
        for (auto it = counters_.constBegin(); it != counters_.constEnd(); ++it)
            cmap[it.key()] = it.value();
        snap["counters"] = cmap;

        QVariantMap gmap;
        for (auto it = gauges_.constBegin(); it != gauges_.constEnd(); ++it)
            gmap[it.key()] = it.value();
        snap["gauges"] = gmap;
        return snap;
    }

    /// 导出 Prometheus 格式文本
    QString toPrometheusText() const {
        QMutexLocker lock(&mutex_);
        QStringList lines;

        for (auto it = counters_.constBegin(); it != counters_.constEnd(); ++it) {
            lines << QString("# HELP %1 Counter metric").arg(it.key());
            lines << QString("# TYPE %1 counter").arg(it.key());
            lines << QString("%1 %2").arg(it.key()).arg(it.value());
        }
        for (auto it = gauges_.constBegin(); it != gauges_.constEnd(); ++it) {
            lines << QString("# HELP %1 Gauge metric").arg(it.key());
            lines << QString("# TYPE %1 gauge").arg(it.key());
            lines << QString("%1 %2").arg(it.key()).arg(it.value());
        }
        for (auto it = histograms_.constBegin(); it != histograms_.constEnd(); ++it) {
            const auto& h = it.value();
            lines << QString("# HELP %1 Histogram metric").arg(it.key());
            lines << QString("# TYPE %1 histogram").arg(it.key());
            lines << QString("%1_count %2").arg(it.key()).arg(h.count);
            lines << QString("%1_sum %2").arg(it.key()).arg(h.sum);
            lines << QString("%1_min %2").arg(it.key()).arg(h.min);
            lines << QString("%1_max %2").arg(it.key()).arg(h.max);
            if (h.count > 0)
                lines << QString("%1_avg %2").arg(it.key()).arg(h.sum / h.count);
        }

        return lines.join('\n') + '\n';
    }

    void reset() {
        QMutexLocker lock(&mutex_);
        counters_.clear();
        gauges_.clear();
        histograms_.clear();
    }

private:
    struct Histogram {
        QList<double> values;
        double sum = 0.0;
        double min = 0.0;
        double max = 0.0;
        int count = 0;
    };

    mutable QMutex mutex_;
    QHash<QString, int> counters_;
    QHash<QString, double> gauges_;
    QHash<QString, Histogram> histograms_;
};

} // namespace eon::core
