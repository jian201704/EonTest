#pragma once

#include <QString>

#include "eon/domain/WorkflowDefinition.h"
#include "eon/runtime/WorkflowEngine.h"

namespace eon::application {

class RunWorkflowUseCase {
public:
    explicit RunWorkflowUseCase(eon::runtime::WorkflowEngine* engine);

    bool runMinimal(const QString& pluginDirectory, QString* errorMessage = nullptr);
    bool runWorkflowDefinition(
        const QString& pluginDirectory,
        const eon::domain::WorkflowDefinition& workflowDefinition,
        QString* errorMessage = nullptr
    );

private:
    eon::runtime::WorkflowEngine* engine_ = nullptr;
};

} // namespace eon::application
