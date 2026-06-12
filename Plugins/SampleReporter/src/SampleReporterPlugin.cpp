#include <QObject>

#include "eon/sdk/IActivityPlugin.h"

class SampleReporterPlugin final : public QObject, public eon::sdk::IReporterPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IREPORTERPLUGIN_IID FILE "samplereporter.json")
    Q_INTERFACES(eon::sdk::IReporterPlugin)

public:
    QString id() const override {
        return "sample.reporter";
    }

    bool report(const eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        Q_UNUSED(errorMessage)
        Q_UNUSED(context)
        return true;
    }
};

#include "SampleReporterPlugin.moc"
