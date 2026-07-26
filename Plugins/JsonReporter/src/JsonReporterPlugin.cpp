#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include "eon/sdk/IStepPlugin.h"

class JsonReporterPlugin final : public QObject, public eon::sdk::IReporterPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IREPORTERPLUGIN_IID FILE "jsonreporter.json")
    Q_INTERFACES(eon::sdk::IReporterPlugin)

public:
    QString id() const override { return "eon.reporter.json"; }

    bool report(const eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        QJsonObject report;
        report["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        report["reporter"] = "eon.reporter.json";
        report["workflowId"] = context.workflowId;

        // 提取所有步骤结果（context.data 中包含 _currentStepId, _executionMode 等）
        QJsonObject stepData;
        for (auto it = context.data.constBegin(); it != context.data.constEnd(); ++it) {
            if (it.key().startsWith('_')) continue; // 跳过内部字段
            stepData[it.key()] = QJsonValue::fromVariant(it.value());
        }
        report["stepOutput"] = stepData;

        // 写入文件
        const QString outputDir = context.data.value("_reportOutputDir",
            QDir::currentPath() + "/reports").toString();
        QDir().mkpath(outputDir);

        const QString filename = QString("%1/%2_%3.json")
            .arg(outputDir)
            .arg(context.workflowId)
            .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss"));

        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly)) {
            errorMessage = "Cannot write report: " + filename;
            return false;
        }

        file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
        file.close();

        qInfo() << "[JsonReporter] Report saved:" << filename;
        return true;
    }
};

#include "JsonReporterPlugin.moc"
