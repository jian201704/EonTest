#pragma once

#include <QString>
#include <QUuid>
#include <QDateTime>
#include <QVariantMap>

namespace eon::core {

// ============================================================================
// Entity — 领域实体基类（有唯一标识）
// ============================================================================
class Entity {
public:
    virtual ~Entity() = default;

    QString id() const { return id_; }
    void setId(const QString& id) { id_ = id; }

    bool operator==(const Entity& other) const {
        return id_ == other.id_;
    }
    bool operator!=(const Entity& other) const {
        return !(*this == other);
    }

protected:
    QString id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
};

// ============================================================================
// ValueObject — 值对象基类（无标识，按值相等）
// ============================================================================
class ValueObject {
public:
    virtual ~ValueObject() = default;
    virtual bool equals(const ValueObject& other) const = 0;

    bool operator==(const ValueObject& other) const {
        return equals(other);
    }
    bool operator!=(const ValueObject& other) const {
        return !equals(other);
    }
};

// ============================================================================
// DomainEvent — 领域事件基类
// ============================================================================
class DomainEvent {
public:
    virtual ~DomainEvent() = default;

    QString eventId() const { return eventId_; }
    QString eventName() const { return eventName_; }
    QDateTime occurredAt() const { return occurredAt_; }
    QString aggregateId() const { return aggregateId_; }

    virtual QVariantMap toVariantMap() const {
        return {
            {"eventId", eventId_},
            {"eventName", eventName_},
            {"occurredAt", occurredAt_.toString(Qt::ISODateWithMs)},
            {"aggregateId", aggregateId_}
        };
    }

protected:
    explicit DomainEvent(const QString& eventName, const QString& aggregateId = {})
        : eventId_(QUuid::createUuid().toString(QUuid::WithoutBraces))
        , eventName_(eventName)
        , occurredAt_(QDateTime::currentDateTimeUtc())
        , aggregateId_(aggregateId) {}

private:
    QString eventId_;
    QString eventName_;
    QDateTime occurredAt_;
    QString aggregateId_;
};

// ============================================================================
// Measurement — 单次测量值（值对象）
// ============================================================================
class Measurement : public ValueObject {
public:
    Measurement() = default;

    Measurement(const QString& name, double value, const QString& unit,
                quint64 timestampUs = 0)
        : name_(name), value_(value), unit_(unit),
          timestampUs_(timestampUs > 0 ? timestampUs
                       : static_cast<quint64>(
                           QDateTime::currentMSecsSinceEpoch() * 1000)) {}

    QString name() const { return name_; }
    double value() const { return value_; }
    QString unit() const { return unit_; }
    quint64 timestampUs() const { return timestampUs_; }

    bool equals(const ValueObject& other) const override {
        const auto* m = dynamic_cast<const Measurement*>(&other);
        if (!m) return false;
        return name_ == m->name_ && qFuzzyCompare(value_, m->value_) &&
               unit_ == m->unit_ && timestampUs_ == m->timestampUs_;
    }

    QVariantMap toVariantMap() const {
        return {
            {"name", name_},
            {"value", value_},
            {"unit", unit_},
            {"timestampUs", static_cast<qint64>(timestampUs_)}
        };
    }

private:
    QString name_;
    double value_ = 0.0;
    QString unit_;
    quint64 timestampUs_ = 0;
};

// ============================================================================
// PassFail — 判定结果
// ============================================================================
enum class PassFail {
    Pass,
    Fail,
    Skipped,
    Error,
    NotEvaluated
};

inline QString passFailToString(PassFail pf) {
    switch (pf) {
    case PassFail::Pass:   return "pass";
    case PassFail::Fail:   return "fail";
    case PassFail::Skipped: return "skipped";
    case PassFail::Error:  return "error";
    case PassFail::NotEvaluated: return "not_evaluated";
    }
    return "unknown";
}

inline PassFail passFailFromString(const QString& text) {
    const QString lower = text.toLower().trimmed();
    if (lower == "pass")   return PassFail::Pass;
    if (lower == "fail")   return PassFail::Fail;
    if (lower == "skipped") return PassFail::Skipped;
    if (lower == "error")  return PassFail::Error;
    return PassFail::NotEvaluated;
}

} // namespace eon::core
