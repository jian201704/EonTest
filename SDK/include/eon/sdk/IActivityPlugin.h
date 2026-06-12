#pragma once

#include <QString>
#include <QVariantMap>
#include <QtPlugin>

namespace eon::sdk {

struct WorkflowContext {
    QString workflowId;
    QVariantMap data;
};

class IActivityPlugin {
public:
    virtual ~IActivityPlugin() = default;

    virtual QString id() const = 0;
    virtual bool execute(WorkflowContext& context, QString& errorMessage) = 0;
};

} // namespace eon::sdk

#define EON_IACTIVITYPLUGIN_IID "com.eontest.sdk.IActivityPlugin/1.0"
Q_DECLARE_INTERFACE(eon::sdk::IActivityPlugin, EON_IACTIVITYPLUGIN_IID)
