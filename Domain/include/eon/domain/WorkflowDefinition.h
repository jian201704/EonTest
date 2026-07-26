#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

namespace eon::domain {

enum class FailurePolicy {
    FailFast,
    ContinueOnError
};

/// <summary>
/// 步骤级中断条件（对标 OpenTAP BreakCondition）。
/// 位掩码标志，可组合使用。
/// </summary>
enum class BreakCondition {
    Inherit = 0,             // 继承引擎默认（不中断）
    BreakOnInconclusive = 1, // Inconclusive 时中断后续步骤
    BreakOnFail = 2,         // Fail 时中断后续步骤
    BreakOnError = 4,        // Error 时中断后续步骤
    BreakOnPass = 8          // Pass 时中断（调试用）
};

inline int breakConditionMask(BreakCondition c) { return static_cast<int>(c); }

struct StepExecutionPolicy {
    int maxRetries = 0;
    int timeoutMs = 0;
    FailurePolicy failurePolicy = FailurePolicy::FailFast;
};

struct ActivityStep {
    QString stepId;
    QString pluginId;
    QString parallelGroupId;
    StepExecutionPolicy policy;
    QString conditionKey;
    QString conditionEquals;
    QString compensationStepId;
    QString onSuccessStepId;
    QString onFailureStepId;
    QString onSkippedStepId;
    QVariantMap initialData; // 步骤级参数，执行时合并到 context.data

    /// 中断条件位掩码（默认 Inherit）
    int breakCondition = 0;
};

struct WorkflowDefinition {
    QString workflowId;
    QString entryStepId;
    QVariantMap initialData;
    QList<ActivityStep> steps;

    /// DUT 插件 ID（如 "simple.dut"），引擎据此查找并注入到 WorkflowContext
    QString dutPluginId;
    /// DUT 配置参数（如序列号、端口等）
    QVariantMap dutConfig;
};

inline WorkflowDefinition createMinimalWorkflowDefinition() {
    return WorkflowDefinition{
        .workflowId = "mvp-workflow",
        .entryStepId = "step.sample",
        .initialData = {},
        .steps = {
            ActivityStep{
                .stepId = "step.sample",
                .pluginId = "sample.activity",
                .parallelGroupId = "",
                .policy = StepExecutionPolicy{
                    .maxRetries = 0,
                    .timeoutMs = 0,
                    .failurePolicy = FailurePolicy::FailFast
                },
                .conditionKey = "",
                .conditionEquals = "",
                .compensationStepId = "",
                .onSuccessStepId = "",
                .onFailureStepId = "",
                .onSkippedStepId = "",
                .initialData = {}
            }
        }
    };
}

} // namespace eon::domain
