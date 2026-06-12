#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

namespace eon::domain {

enum class FailurePolicy {
    FailFast,
    ContinueOnError
};

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
};

struct WorkflowDefinition {
    QString workflowId;
    QString entryStepId;
    QVariantMap initialData;
    QList<ActivityStep> steps;
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
                .onSkippedStepId = ""
            }
        }
    };
}

} // namespace eon::domain
