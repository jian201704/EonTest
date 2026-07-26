#pragma once

#include <QString>
#include <QList>
#include <QSet>
#include <QDateTime>

namespace eon::orchestrator {

// ============================================================================
// TaskDescriptor — 调度器视角的任务描述（轻量）
// ============================================================================
struct TaskDescriptor {
    int taskId = 0;
    int priority = 0;           // 优先级（越大越优先
    qint64 enqueueOrder = 0;    // 入队序号（FIFO 用）
    qint64 nextRunAtMs = 0;     // 下次可运行时间（退避/延时用）
    qint64 deadlineMs = 0;      // 截止时间（Deadline 策略用，0=无
    QStringList resourceLocks;  // 所需资源锁
    bool isPending = true;      // 是否待调度
};

// ============================================================================
// SchedulingPolicy — 调度策略抽象接口
// ============================================================================
class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;

    /// 策略名称
    virtual QString name() const = 0;

    /// 从待调度任务列表中选择下一个要执行的任务索引
    /// @param tasks    所有任务列表
    /// @param heldLocks 当前持有的资源锁集合
    /// @param nowMs    当前时间 (ms since epoch)
    /// @return 选中的任务索引，-1 表示无可调度任务
    virtual int selectNext(const QList<TaskDescriptor>& tasks,
                           const QSet<QString>& heldLocks,
                           qint64 nowMs) const = 0;
};

// ============================================================================
// PriorityPolicy — 优先级调度（高优先级优先，同优先级 FIFO）
// ============================================================================
class PriorityPolicy final : public SchedulingPolicy {
public:
    QString name() const override { return "priority"; }

    int selectNext(const QList<TaskDescriptor>& tasks,
                   const QSet<QString>& heldLocks,
                   qint64 nowMs) const override {
        int best = -1;
        for (int i = 0; i < tasks.size(); ++i) {
            const auto& t = tasks.at(i);
            if (!t.isPending) continue;
            if (t.nextRunAtMs > nowMs) continue;

            bool locked = false;
            for (const QString& lock : t.resourceLocks) {
                if (heldLocks.contains(lock)) { locked = true; break; }
            }
            if (locked) continue;

            if (best < 0) { best = i; continue; }

            const auto& s = tasks.at(best);
            // 优先级高的优先
            if (t.priority != s.priority) {
                if (t.priority > s.priority) best = i;
                continue;
            }
            // 同优先级：backoff 早的先跑
            if (t.nextRunAtMs != s.nextRunAtMs) {
                if (t.nextRunAtMs < s.nextRunAtMs) best = i;
                continue;
            }
            // FIFO
            if (t.enqueueOrder < s.enqueueOrder) best = i;
        }
        return best;
    }
};

// ============================================================================
// FifoPolicy — 纯 FIFO 调度
// ============================================================================
class FifoPolicy final : public SchedulingPolicy {
public:
    QString name() const override { return "fifo"; }

    int selectNext(const QList<TaskDescriptor>& tasks,
                   const QSet<QString>& heldLocks,
                   qint64 nowMs) const override {
        int best = -1;
        for (int i = 0; i < tasks.size(); ++i) {
            const auto& t = tasks.at(i);
            if (!t.isPending) continue;
            if (t.nextRunAtMs > nowMs) continue;

            bool locked = false;
            for (const QString& lock : t.resourceLocks) {
                if (heldLocks.contains(lock)) { locked = true; break; }
            }
            if (locked) continue;

            if (best < 0 || t.enqueueOrder < tasks.at(best).enqueueOrder) {
                best = i;
            }
        }
        return best;
    }
};

// ============================================================================
// DeadlinePolicy — 最早截止时间优先 (EDF)
// ============================================================================
class DeadlinePolicy final : public SchedulingPolicy {
public:
    QString name() const override { return "deadline"; }

    int selectNext(const QList<TaskDescriptor>& tasks,
                   const QSet<QString>& heldLocks,
                   qint64 nowMs) const override {
        int best = -1;
        for (int i = 0; i < tasks.size(); ++i) {
            const auto& t = tasks.at(i);
            if (!t.isPending) continue;
            if (t.nextRunAtMs > nowMs) continue;

            bool locked = false;
            for (const QString& lock : t.resourceLocks) {
                if (heldLocks.contains(lock)) { locked = true; break; }
            }
            if (locked) continue;

            // 跳过无截止时间的任务（除非没有带截止时间的任务）
            if (t.deadlineMs <= 0) {
                if (best < 0) best = i;
                continue;
            }

            if (best < 0) { best = i; continue; }

            const auto& s = tasks.at(best);
            if (s.deadlineMs <= 0) { best = i; continue; }

            if (t.deadlineMs < s.deadlineMs) best = i;
        }
        return best;
    }
};

// ============================================================================
// SchedulingPolicyFactory — 策略工厂
// ============================================================================
inline SchedulingPolicy* createPolicy(const QString& name) {
    const QString lower = name.toLower().trimmed();
    if (lower == "fifo")      return new FifoPolicy();
    if (lower == "deadline")  return new DeadlinePolicy();
    return new PriorityPolicy(); // 默认
}

// ============================================================================
// CellHealth — CELL 健康状态
// ============================================================================
struct CellHealth {
    int slotId = 0;
    int taskId = 0;
    qint64 lastHeartbeatMs = 0;  // 最后心跳时间
    qint64 startedAtMs = 0;      // 任务开始时间
    qint64 timeoutMs = 300000;   // 超时阈值 (5 min default)
    bool isAlive = true;         // CELL 是否存活

    bool isTimedOut(qint64 nowMs) const {
        return (nowMs - startedAtMs) > timeoutMs;
    }
};

} // namespace eon::orchestrator
