#pragma once

#include <memory>
#include <vector>

#include <QPluginLoader>
#include <QString>

#include "eon/core/EventBus.h"
#include "eon/sdk/IActivityPlugin.h"

namespace eon::runtime {

class WorkflowEngine {
public:
    explicit WorkflowEngine(eon::core::EventBus* eventBus);

    bool loadPlugins(const QString& pluginDirectory, QString* errorMessage = nullptr);
    bool executeMinimalWorkflow(QString* errorMessage = nullptr);

private:
    struct PluginHandle {
        std::unique_ptr<QPluginLoader> loader;
        eon::sdk::IActivityPlugin* plugin = nullptr;
    };

    eon::core::EventBus* eventBus_ = nullptr;
    std::vector<PluginHandle> plugins_;
};

} // namespace eon::runtime
