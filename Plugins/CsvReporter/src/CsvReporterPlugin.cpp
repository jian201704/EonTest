#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QObject>
#include <QTextStream>

#include "eon/sdk/IStepPlugin.h"

class CsvReporterPlugin final : public QObject, public eon::sdk::IReporterPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IREPORTERPLUGIN_IID FILE "csvreporter.json")
    Q_INTERFACES(eon::sdk::IReporterPlugin)

public:
    QString id() const override { return "eon.reporter.csv"; }

    bool report(const eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        const QString outputDir = context.data.value("_reportOutputDir",
            QDir::currentPath() + "/reports").toString();
        QDir().mkpath(outputDir);

        const QString filename = outputDir + "/test_results.csv";
        const bool isNew = !QFileInfo::exists(filename);

        QFile file(filename);
        if (!file.open(QIODevice::Append | QIODevice::Text)) {
            errorMessage = "Cannot write CSV: " + filename;
            return false;
        }

        QTextStream ts(&file);

        // 表头（仅新文件）
        if (isNew) {
            ts << "timestamp,workflowId,stepId,pluginId,status,attempt,elapsedMs,error,"
                  "sampleResult,qualityGate,measurementStatus,measuredValue,measuredUnit,analysisPassed,analysisMessage,resultItems\n";
        }

        // 从 context.data 提取步骤信息
        const QString currentStepId = context.data.value("_currentStepId").toString();
        const QString executionMode = context.data.value("_executionMode").toString();
        const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

        if (currentStepId.isEmpty()) {
            // workflow 级别汇总行
            ts << timestamp << "," << context.workflowId << ","
               << "SUMMARY" << ",,," << executionMode << ",,,\n";
        } else {
            ts << timestamp << "," << context.workflowId << ","
               << currentStepId << ","
               << context.data.value("pluginId", "").toString() << ","
               << executionMode << ",,,\n";
        }

          // 输出关键字段
          const QString sampleResult = context.data.value("sampleResult").toString();
          const QString qualityGate = context.data.value("qualityGate").toString();
          const QString measurementStatus = context.data.value("measurementStatus").toString();
          const QString measuredValue = context.data.value("measuredValue").toString();
          const QString measuredUnit = context.data.value("measuredUnit").toString();
          const QString analysisPassed = context.data.value("analyze.passed").toString();
          const QString analysisMessage = context.data.value("analyze.message").toString();
          const QString resultItems = QString::fromUtf8(QJsonDocument::fromVariant(
              context.data.value("resultItems")).toJson(QJsonDocument::Compact));

          ts << timestamp << "," << context.workflowId << ","
              << currentStepId << ","
              << context.data.value("pluginId", "").toString() << ","
              << executionMode << ",,,"
              << sampleResult << "," << qualityGate << ","
              << measurementStatus << ","
              << measuredValue << ","
              << measuredUnit << ","
              << analysisPassed << ","
              << analysisMessage << ","
              << resultItems << "\n";

        file.close();
        return true;
    }
};

#include "CsvReporterPlugin.moc"
