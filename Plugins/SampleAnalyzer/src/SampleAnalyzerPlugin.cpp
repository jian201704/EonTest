#include <QObject>
#include <QVariantMap>

#include "eon/sdk/IActivityPlugin.h"

class SampleAnalyzerPlugin final : public QObject, public eon::sdk::IAnalyzerPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IANALYZERPLUGIN_IID FILE "sampleanalyzer.json")
    Q_INTERFACES(eon::sdk::IAnalyzerPlugin)

public:
    QString id() const override {
        return "sample.analyzer";
    }

    bool analyze(const eon::sdk::WorkflowContext& context, QVariantMap& result, QString& errorMessage) override {
        Q_UNUSED(errorMessage)
        result.insert("analyze.sampleResult", context.data.value("sampleResult", "missing"));
        result.insert("analyze.qualityGate", context.data.value("qualityGate", "unknown"));
        result.insert("analyze.workflowId", context.workflowId);
        return true;
    }
};

#include "SampleAnalyzerPlugin.moc"
