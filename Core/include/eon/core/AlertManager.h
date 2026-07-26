#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <functional>

namespace eon::core {

// ============================================================================
// AlertRule — 告警规则
// ============================================================================
struct AlertRule {
    QString name;                // 规则名
    QString metric;              // 监控的指标名
    QString condition;           // ">=" | ">" | "<=" | "<" | "=="
    double threshold = 0.0;      // 阈值
    int cooldownMs = 60000;      // 冷却时间 (ms)，避免告警风暴
    bool enabled = true;
};

// ============================================================================
// Alert — 告警事件
// ============================================================================
struct Alert {
    QString ruleName;
    QString metric;
    double currentValue = 0.0;
    double threshold = 0.0;
    QString condition;
    qint64 firedAtMs = 0;
    bool acknowledged = false;
};

// ============================================================================
// AlertManager — 告警管理器
//
// 定期检查指标是否触发告警规则，去重（冷却），支持回调通知
// ============================================================================
class AlertManager : public QObject {
    Q_OBJECT

public:
    using AlertCallback = std::function<void(const Alert&)>;

    explicit AlertManager(QObject* parent = nullptr);
    ~AlertManager() override;

    /// 添加告警规则
    void addRule(const AlertRule& rule);

    /// 移除规则
    void removeRule(const QString& name);

    /// 获取所有规则
    QList<AlertRule> rules() const { return rules_.values(); }

    /// 设置告警回调（可注册多个）
    void setCallback(AlertCallback callback) { callback_ = std::move(callback); }

    /// 手动触发一次评估
    void evaluate();

    /// 获取当前活跃告警列表
    QList<Alert> activeAlerts() const { return activeAlerts_.values(); }

    /// 确认告警
    void acknowledge(const QString& ruleName);

    /// 开始定时评估（周期 ms）
    void start(int intervalMs = 10000);

    /// 停止定时评估
    void stop();

signals:
    void alertFired(const QVariantMap& alertData);
    void alertCleared(const QString& ruleName);

private:
    QHash<QString, AlertRule> rules_;
    QHash<QString, Alert> activeAlerts_;
    QHash<QString, qint64> lastFiredAt_;
    AlertCallback callback_;
    QTimer* timer_ = nullptr;
};

} // namespace eon::core
