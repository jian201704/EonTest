#include "eon/application/RunWorkflowUseCase.h"

#include "eon/domain/WorkflowDefinition.h"

namespace eon::application {

RunWorkflowUseCase::RunWorkflowUseCase(eon::runtime::WorkflowEngine* engine)
    : engine_(engine) {}

bool RunWorkflowUseCase::runMinimal(const QString& pluginDirectory, QString* errorMessage) {
    return runWorkflowDefinition(
        pluginDirectory,
        eon::domain::createMinimalWorkflowDefinition(),
        errorMessage
    );
}

bool RunWorkflowUseCase::runWorkflowDefinition(
    const QString& pluginDirectory,
    const eon::domain::WorkflowDefinition& workflowDefinition,
    QString* errorMessage
) {
    if (engine_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "WorkflowEngine is null.";
        }
        return false;
    }

    if (!engine_->loadPlugins(pluginDirectory, errorMessage)) {
        return false;
    }

    return engine_->executeWorkflow(workflowDefinition, errorMessage);
}

} // namespace eon::application
