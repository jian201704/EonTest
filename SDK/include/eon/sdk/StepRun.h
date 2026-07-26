#pragma once

#include <QDateTime>
#include <QString>

#include "Verdict.h"

namespace eon::sdk {

/// <summary>
/// 单步执行运行时记录。
/// 对标 OpenTAP TestStepRun。
///
/// 每次 executeStep() 调用创建一个 StepRun，记录执行详情。
/// WorkflowEngine 在步骤执行前后自动更新此对象。
/// </summary>
struct StepRun {
    QString stepId;          // 当前步骤 ID
    QString pluginId;        // 对应插件 ID
    QDateTime startedAt;     // 执行开始时间
    qint64 elapsedMs = 0;    // 执行耗时 (毫秒)
    Verdict verdict = Verdict::NotSet; // 步骤判定结果
    int attemptCount = 0;    // 当前是第几次重试
    QString errorMessage;    // 错误信息（失败时填充）
    QString outputSummary;   // 输出摘要
};

} // namespace eon::sdk
