#pragma once

#include <QString>
#include <QUuid>
#include <QDateTime>
#include <QHash>
#include <QStack>
#include <QMutex>

namespace eon::core {

// ============================================================================
// TraceId — 全链路追踪标识
// 跨进程传递（通过环境变量/命令行参数/JSON telemetry）
// ============================================================================
class TraceId {
public:
    TraceId() : id_(generateId()) {}
    explicit TraceId(const QString& id) : id_(id.isEmpty() ? generateId() : id) {}

    QString toString() const { return id_; }
    bool isValid() const { return !id_.isEmpty(); }

    static TraceId generate() { return TraceId(); }
    static TraceId fromString(const QString& s) { return TraceId(s); }

    bool operator==(const TraceId& o) const { return id_ == o.id_; }

private:
    static QString generateId() {
        return QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);
    }
    QString id_;
};

// ============================================================================
// SpanId — 单次操作跨度标识
// ============================================================================
class SpanId {
public:
    SpanId() : id_(generateId()) {}
    explicit SpanId(const QString& id) : id_(id.isEmpty() ? generateId() : id) {}

    QString toString() const { return id_; }

    static SpanId generate() { return SpanId(); }

private:
    static QString generateId() {
        return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    }
    QString id_;
};

// ============================================================================
// TraceContext — 当前追踪上下文（线程级）
// ============================================================================
class TraceContext {
public:
    static TraceContext& instance() { static TraceContext ctx; return ctx; }

    void beginTrace(const TraceId& traceId) {
        QMutexLocker lock(&mutex_);
        currentTraceId_ = traceId;
        spanStack_.clear();
    }

    TraceId currentTraceId() const {
        QMutexLocker lock(&mutex_);
        return currentTraceId_;
    }

    SpanId beginSpan(const QString& name) {
        QMutexLocker lock(&mutex_);
        SpanId sid = SpanId::generate();
        SpanRecord rec{sid, name, QDateTime::currentMSecsSinceEpoch(), 0};
        spanStack_.push(rec);
        return sid;
    }

    void endSpan() {
        QMutexLocker lock(&mutex_);
        if (!spanStack_.isEmpty()) {
            SpanRecord& rec = spanStack_.top();
            rec.elapsedMs = QDateTime::currentMSecsSinceEpoch() - rec.startMs;
            spanStack_.pop();
        }
    }

    void injectToEnv() const;
    static TraceId extractFromEnv();

private:
    struct SpanRecord {
        SpanId spanId;
        QString name;
        qint64 startMs = 0;
        qint64 elapsedMs = 0;
    };

    mutable QMutex mutex_;
    TraceId currentTraceId_ = TraceId::generate();
    QStack<SpanRecord> spanStack_;
};

inline void TraceContext::injectToEnv() const {
    qputenv("EON_TRACE_ID", currentTraceId_.toString().toLocal8Bit());
}

inline TraceId TraceContext::extractFromEnv() {
    const QString id = QString::fromLocal8Bit(qgetenv("EON_TRACE_ID"));
    return id.isEmpty() ? TraceId::generate() : TraceId::fromString(id);
}

} // namespace eon::core
