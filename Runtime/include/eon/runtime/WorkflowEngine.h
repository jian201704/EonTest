#pragma once

#include <memory>
#include <vector>

#include <QHash>
#include <QPluginLoader>
#include <QString>

#include "eon/core/EventBus.h"
#include "eon/domain/WorkflowDefinition.h"
#include "eon/sdk/IActivityPlugin.h"

namespace eon::runtime {

class WorkflowEngine {
public:
    explicit WorkflowEngine(eon::core::EventBus* eventBus);

    bool loadPlugins(const QString& pluginDirectory, QString* errorMessage = nullptr);
    bool executeWorkflow(const eon::domain::WorkflowDefinition& workflow, QString* errorMessage = nullptr);
    bool executeMinimalWorkflow(QString* errorMessage = nullptr);

private:
    struct PluginHandle {
        std::unique_ptr<QPluginLoader> loader;
        QString pluginId;
        QString contractType;
        QString contractVersion;
    };

    eon::sdk::IStepPlugin* findStepPluginById(const QString& pluginId) const;

    eon::core::EventBus* eventBus_ = nullptr;
    std::vector<PluginHandle> plugins_;
    QHash<QString, eon::sdk::IStepPlugin*> stepPluginsById_;
    QHash<QString, eon::sdk::IAnalyzerPlugin*> analyzerPluginsById_;
    QHash<QString, eon::sdk::IReporterPlugin*> reporterPluginsById_;
};

} // namespace eon::runtime
