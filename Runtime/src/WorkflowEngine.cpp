#include "eon/runtime/WorkflowEngine.h"

#include <QDir>
#include <QFileInfoList>

namespace eon::runtime {

WorkflowEngine::WorkflowEngine(eon::core::EventBus* eventBus)
    : eventBus_(eventBus) {}

bool WorkflowEngine::loadPlugins(const QString& pluginDirectory, QString* errorMessage) {
    plugins_.clear();

    const QDir directory(pluginDirectory);
    if (!directory.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("Plugin directory does not exist: %1").arg(pluginDirectory);
        }
        return false;
    }

    const QFileInfoList files = directory.entryInfoList(QDir::Files);
    for (const QFileInfo& file : files) {
        auto loader = std::make_unique<QPluginLoader>(file.absoluteFilePath());
        QObject* instance = loader->instance();
        if (instance == nullptr) {
            continue;
        }

        auto* plugin = qobject_cast<eon::sdk::IActivityPlugin*>(instance);
        if (plugin == nullptr) {
            continue;
        }

        plugins_.push_back(PluginHandle{
            std::move(loader),
            plugin
        });
    }

    if (plugins_.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QString("No compatible plugins loaded from: %1").arg(pluginDirectory);
        }
        return false;
    }

    return true;
}

bool WorkflowEngine::executeMinimalWorkflow(QString* errorMessage) {
    if (eventBus_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "EventBus is null.";
        }
        return false;
    }

    eon::sdk::WorkflowContext context;
    context.workflowId = "mvp-workflow";

    eventBus_->publish("workflow.started", {
        {"workflowId", context.workflowId}
    });

    for (const auto& handle : plugins_) {
        const QString pluginId = handle.plugin->id();
        eventBus_->publish("activity.started", {
            {"pluginId", pluginId}
        });

        QString pluginError;
        if (!handle.plugin->execute(context, pluginError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QString("Plugin '%1' failed: %2").arg(pluginId, pluginError);
            }
            eventBus_->publish("workflow.failed", {
                {"pluginId", pluginId},
                {"error", pluginError}
            });
            return false;
        }

        eventBus_->publish("activity.finished", {
            {"pluginId", pluginId}
        });
    }

    eventBus_->publish("workflow.finished", {
        {"workflowId", context.workflowId}
    });
    return true;
}

} // namespace eon::runtime
