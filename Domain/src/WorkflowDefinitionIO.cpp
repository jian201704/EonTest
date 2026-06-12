#include "eon/domain/WorkflowDefinitionIO.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace eon::domain {

namespace {

bool failParse(QString* errorMessage, const QString& message) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

QString requiredString(const QJsonObject& object, const QString& key) {
    return object.value(key).toString().trimmed();
}

bool parseFailurePolicy(const QString& value, FailurePolicy* policyOut) {
    if (policyOut == nullptr) {
        return false;
    }
    if (value.isEmpty() || value == "fail_fast") {
        *policyOut = FailurePolicy::FailFast;
        return true;
    }
    if (value == "continue_on_error") {
        *policyOut = FailurePolicy::ContinueOnError;
        return true;
    }
    return false;
}

} // namespace

bool parseWorkflowDefinitionJson(
    const QByteArray& jsonData,
    WorkflowDefinition* workflowDefinition,
    QString* errorMessage
) {
    if (workflowDefinition == nullptr) {
        return failParse(errorMessage, "WorkflowDefinition output is null.");
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return failParse(errorMessage, QString("Invalid workflow JSON: %1").arg(parseError.errorString()));
    }
    if (!document.isObject()) {
        return failParse(errorMessage, "Workflow JSON root must be an object.");
    }

    const QJsonObject root = document.object();
    WorkflowDefinition workflow;
    workflow.workflowId = requiredString(root, "workflowId");
    workflow.entryStepId = requiredString(root, "entryStepId");
    workflow.initialData = root.value("initialData").toObject().toVariantMap();

    if (workflow.workflowId.isEmpty()) {
        return failParse(errorMessage, "workflowId is required.");
    }

    const QJsonValue stepsValue = root.value("steps");
    if (!stepsValue.isArray()) {
        return failParse(errorMessage, "steps must be an array.");
    }

    const QJsonArray stepsArray = stepsValue.toArray();
    if (stepsArray.isEmpty()) {
        return failParse(errorMessage, "steps must not be empty.");
    }

    for (int index = 0; index < stepsArray.size(); ++index) {
        const QJsonValue stepValue = stepsArray.at(index);
        if (!stepValue.isObject()) {
            return failParse(errorMessage, QString("steps[%1] must be an object.").arg(index));
        }

        const QJsonObject stepObject = stepValue.toObject();
        ActivityStep step;
        step.stepId = requiredString(stepObject, "stepId");
        step.pluginId = requiredString(stepObject, "pluginId");
        step.parallelGroupId = requiredString(stepObject, "parallelGroupId");
        step.onSuccessStepId = requiredString(stepObject, "onSuccessStepId");
        step.onFailureStepId = requiredString(stepObject, "onFailureStepId");
        step.onSkippedStepId = requiredString(stepObject, "onSkippedStepId");
        step.conditionKey = requiredString(stepObject, "conditionKey");
        step.conditionEquals = requiredString(stepObject, "conditionEquals");
        step.compensationStepId = requiredString(stepObject, "compensationStepId");

        step.policy.maxRetries = stepObject.value("maxRetries").toInt(0);
        step.policy.timeoutMs = stepObject.value("timeoutMs").toInt(0);
        if (!parseFailurePolicy(requiredString(stepObject, "failurePolicy"), &step.policy.failurePolicy)) {
            return failParse(
                errorMessage,
                QString("steps[%1].failurePolicy must be 'fail_fast' or 'continue_on_error'.").arg(index)
            );
        }

        if (step.stepId.isEmpty()) {
            return failParse(errorMessage, QString("steps[%1].stepId is required.").arg(index));
        }
        if (step.pluginId.isEmpty()) {
            return failParse(errorMessage, QString("steps[%1].pluginId is required.").arg(index));
        }
        if (step.policy.maxRetries < 0) {
            return failParse(errorMessage, QString("steps[%1].maxRetries must be >= 0.").arg(index));
        }
        if (step.policy.timeoutMs < 0) {
            return failParse(errorMessage, QString("steps[%1].timeoutMs must be >= 0.").arg(index));
        }

        workflow.steps.append(step);
    }

    if (workflow.entryStepId.isEmpty()) {
        workflow.entryStepId = workflow.steps.first().stepId;
    }

    *workflowDefinition = workflow;
    return true;
}

} // namespace eon::domain
