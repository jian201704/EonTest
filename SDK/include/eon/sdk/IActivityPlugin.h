#pragma once

#include <QString>
#include <QVariantMap>
#include <QtPlugin>

namespace eon::sdk {

struct WorkflowContext {
    QString workflowId;
    QVariantMap data;
};

class IStepPlugin {
public:
    virtual ~IStepPlugin() = default;

    virtual QString id() const = 0;
    virtual bool executeStep(WorkflowContext& context, QString& errorMessage) = 0;
};

class IAnalyzerPlugin {
public:
    virtual ~IAnalyzerPlugin() = default;

    virtual QString id() const = 0;
    virtual bool analyze(const WorkflowContext& context, QVariantMap& result, QString& errorMessage) = 0;
};

class IReporterPlugin {
public:
    virtual ~IReporterPlugin() = default;

    virtual QString id() const = 0;
    virtual bool report(const WorkflowContext& context, QString& errorMessage) = 0;
};

} // namespace eon::sdk

#define EON_ISTEPPLUGIN_IID "com.eontest.sdk.IStepPlugin/1.0"
#define EON_IANALYZERPLUGIN_IID "com.eontest.sdk.IAnalyzerPlugin/1.0"
#define EON_IREPORTERPLUGIN_IID "com.eontest.sdk.IReporterPlugin/1.0"

Q_DECLARE_INTERFACE(eon::sdk::IStepPlugin, EON_ISTEPPLUGIN_IID)
Q_DECLARE_INTERFACE(eon::sdk::IAnalyzerPlugin, EON_IANALYZERPLUGIN_IID)
Q_DECLARE_INTERFACE(eon::sdk::IReporterPlugin, EON_IREPORTERPLUGIN_IID)
