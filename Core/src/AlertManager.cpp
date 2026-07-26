#include "eon/core/AlertManager.h"
#include "eon/core/Metrics.h"

#include <QDateTime>

namespace eon::core {

AlertManager::AlertManager(QObject* parent)
    : QObject(parent)
    , timer_(new QTimer(this))
{
    connect(timer_, &QTimer::timeout, this, &AlertManager::evaluate);
}

AlertManager::~AlertManager() { stop(); }

void AlertManager::addRule(const AlertRule& rule) {
    rules_[rule.name] = rule;
}

void AlertManager::removeRule(const QString& name) {
    rules_.remove(name);
    activeAlerts_.remove(name);
}

void AlertManager::evaluate() {
    auto& metrics = MetricsCollector::instance();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto it = rules_.cbegin(); it != rules_.cend(); ++it) {
        const AlertRule& rule = it.value();
        if (!rule.enabled) continue;

        // 冷却检查
        const qint64 last = lastFiredAt_.value(rule.name, 0);
        if (now - last < rule.cooldownMs) continue;

        const double v = metrics.gaugeValue(rule.metric);

        bool fired = false;
        if (rule.condition == ">=")      fired = (v >= rule.threshold);
        else if (rule.condition == ">")  fired = (v > rule.threshold);
        else if (rule.condition == "<=") fired = (v <= rule.threshold);
        else if (rule.condition == "<")  fired = (v < rule.threshold);
        else if (rule.condition == "==") fired = (qFuzzyCompare(v, rule.threshold));

        if (fired) {
            Alert alert;
            alert.ruleName = rule.name;
            alert.metric = rule.metric;
            alert.currentValue = v;
            alert.threshold = rule.threshold;
            alert.condition = rule.condition;
            alert.firedAtMs = now;

            activeAlerts_[rule.name] = alert;
            lastFiredAt_[rule.name] = now;

            QVariantMap data;
            data["rule"] = rule.name; data["metric"] = rule.metric;
            data["value"] = v; data["threshold"] = rule.threshold;
            data["condition"] = rule.condition;
            emit alertFired(data);

            if (callback_) callback_(alert);
        } else if (activeAlerts_.contains(rule.name)) {
            activeAlerts_.remove(rule.name);
            emit alertCleared(rule.name);
        }
    }
}

void AlertManager::acknowledge(const QString& ruleName) {
    if (activeAlerts_.contains(ruleName)) {
        activeAlerts_[ruleName].acknowledged = true;
    }
}

void AlertManager::start(int intervalMs) {
    timer_->start(intervalMs);
}

void AlertManager::stop() {
    timer_->stop();
}

} // namespace eon::core
