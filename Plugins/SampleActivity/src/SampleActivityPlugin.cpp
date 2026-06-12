#include <QObject>
#include <QVariantMap>

#include "eon/sdk/IActivityPlugin.h"

class SampleActivityPlugin final : public QObject, public eon::sdk::IActivityPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IACTIVITYPLUGIN_IID FILE "sampleactivity.json")
    Q_INTERFACES(eon::sdk::IActivityPlugin)

public:
    QString id() const override {
        return "sample.activity";
    }

    bool execute(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        Q_UNUSED(errorMessage)
        context.data.insert("sampleResult", "ok");
        return true;
    }
};

#include "SampleActivityPlugin.moc"
