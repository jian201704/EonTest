#include <QObject>
#include <QVariantMap>

#include "eon/sdk/IActivityPlugin.h"

class SampleActivityPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "sampleactivity.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override {
        return "sample.activity";
    }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        const QString currentStepId = context.data.value("_currentStepId").toString();
        const QString forcedFailStepId = context.data.value("forceFailStepId").toString();

        if (!forcedFailStepId.isEmpty() && forcedFailStepId == currentStepId) {
            errorMessage = QString("forceFailStepId matched '%1'.").arg(currentStepId);
            return false;
        }

        if (context.data.value("forceFail").toBool()) {
            errorMessage = "forceFail is true.";
            return false;
        }

        if (currentStepId == "step.compensate") {
            context.data.insert("forceFailStepId", "");
            context.data.insert("compensated", true);
        }

        context.data.insert("sampleResult", "ok");
        context.data.insert("qualityGate", "pass");
        return true;
    }
};

#include "SampleActivityPlugin.moc"
