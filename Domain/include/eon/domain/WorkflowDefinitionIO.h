#pragma once

#include <QByteArray>
#include <QString>

#include "eon/domain/WorkflowDefinition.h"

namespace eon::domain {

bool parseWorkflowDefinitionJson(
    const QByteArray& jsonData,
    WorkflowDefinition* workflowDefinition,
    QString* errorMessage = nullptr
);

} // namespace eon::domain
