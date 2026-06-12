#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "eon/application/RunWorkflowUseCase.h"
#include "eon/core/EventBus.h"
#include "eon/domain/WorkflowDefinitionIO.h"

namespace {

void emitTelemetry(const QString& eventName, const QJsonObject& fields = {}) {
    QJsonObject payload{
        {"source", "runtime-worker"},
        {"event", eventName},
        {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
    };

    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        payload.insert(it.key(), it.value());
    }

    qInfo().noquote() << QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const QString pluginDirectory = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QDir(QCoreApplication::applicationDirPath()).filePath("../Plugins");
    const QString workflowFilePath = (argc > 2)
        ? QString::fromLocal8Bit(argv[2])
        : QString();

    int activityStartedCount = 0;
    int activityFinishedCount = 0;
    int activityRetryCount = 0;
    int activityFailedCount = 0;
    int activitySkippedCount = 0;
    int compensationStartedCount = 0;
    int compensationFinishedCount = 0;
    int compensationFailedCount = 0;
    int parallelBatchStartedCount = 0;
    int parallelBatchFinishedCount = 0;
    int analyzerStartedCount = 0;
    int analyzerFinishedCount = 0;
    int reporterStartedCount = 0;
    int reporterFinishedCount = 0;

    eon::core::EventBus eventBus;
    eventBus.subscribe("workflow.started", [](const QVariantMap& payload) {
        emitTelemetry("workflow.started", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("activity.started", [&activityStartedCount](const QVariantMap& payload) {
        activityStartedCount += 1;
        emitTelemetry("activity.started", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("activity.retry", [&activityRetryCount](const QVariantMap& payload) {
        activityRetryCount += 1;
        emitTelemetry("activity.retry", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("activity.failed", [&activityFailedCount](const QVariantMap& payload) {
        activityFailedCount += 1;
        emitTelemetry("activity.failed", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("activity.skipped", [&activitySkippedCount](const QVariantMap& payload) {
        activitySkippedCount += 1;
        emitTelemetry("activity.skipped", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("activity.finished", [&activityFinishedCount](const QVariantMap& payload) {
        activityFinishedCount += 1;
        emitTelemetry("activity.finished", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("workflow.finished", [](const QVariantMap& payload) {
        emitTelemetry("workflow.finished", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("workflow.failed", [](const QVariantMap& payload) {
        emitTelemetry("workflow.failed", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("compensation.started", [&compensationStartedCount](const QVariantMap& payload) {
        compensationStartedCount += 1;
        emitTelemetry("compensation.started", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("compensation.finished", [&compensationFinishedCount](const QVariantMap& payload) {
        compensationFinishedCount += 1;
        emitTelemetry("compensation.finished", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("compensation.failed", [&compensationFailedCount](const QVariantMap& payload) {
        compensationFailedCount += 1;
        emitTelemetry("compensation.failed", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("parallel.batch.started", [&parallelBatchStartedCount](const QVariantMap& payload) {
        parallelBatchStartedCount += 1;
        emitTelemetry("parallel.batch.started", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("parallel.batch.finished", [&parallelBatchFinishedCount](const QVariantMap& payload) {
        parallelBatchFinishedCount += 1;
        emitTelemetry("parallel.batch.finished", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("analyzer.started", [&analyzerStartedCount](const QVariantMap& payload) {
        analyzerStartedCount += 1;
        emitTelemetry("analyzer.started", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("analyzer.finished", [&analyzerFinishedCount](const QVariantMap& payload) {
        analyzerFinishedCount += 1;
        emitTelemetry("analyzer.finished", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("reporter.started", [&reporterStartedCount](const QVariantMap& payload) {
        reporterStartedCount += 1;
        emitTelemetry("reporter.started", QJsonObject::fromVariantMap(payload));
    });
    eventBus.subscribe("reporter.finished", [&reporterFinishedCount](const QVariantMap& payload) {
        reporterFinishedCount += 1;
        emitTelemetry("reporter.finished", QJsonObject::fromVariantMap(payload));
    });

    eon::runtime::WorkflowEngine engine(&eventBus);
    eon::application::RunWorkflowUseCase useCase(&engine);

    emitTelemetry("worker.started", {
        {"pluginDirectory", pluginDirectory},
        {"workflowFilePath", workflowFilePath}
    });

    QString error;
    eon::domain::WorkflowDefinition workflowDefinition;
    bool runSucceeded = false;
    if (workflowFilePath.isEmpty()) {
        runSucceeded = useCase.runMinimal(pluginDirectory, &error);
    } else {
        QFile workflowFile(workflowFilePath);
        if (!workflowFile.open(QIODevice::ReadOnly)) {
            error = QString("Cannot open workflow file '%1'.").arg(workflowFilePath);
        } else if (!eon::domain::parseWorkflowDefinitionJson(
                       workflowFile.readAll(),
                       &workflowDefinition,
                       &error
                   )) {
            error = QString("Cannot parse workflow file '%1': %2").arg(workflowFilePath, error);
        } else {
            runSucceeded = useCase.runWorkflowDefinition(pluginDirectory, workflowDefinition, &error);
        }
    }

    if (!runSucceeded) {
        emitTelemetry("worker.finished", {
            {"status", "failed"},
            {"activitiesStarted", activityStartedCount},
            {"activitiesFinished", activityFinishedCount},
            {"activityRetries", activityRetryCount},
            {"activityFailed", activityFailedCount},
            {"activitySkipped", activitySkippedCount},
            {"compensationStarted", compensationStartedCount},
            {"compensationFinished", compensationFinishedCount},
            {"compensationFailed", compensationFailedCount},
            {"parallelBatchStarted", parallelBatchStartedCount},
            {"parallelBatchFinished", parallelBatchFinishedCount},
            {"analyzerStarted", analyzerStartedCount},
            {"analyzerFinished", analyzerFinishedCount},
            {"reporterStarted", reporterStartedCount},
            {"reporterFinished", reporterFinishedCount},
            {"error", error}
        });
        qCritical() << "Run workflow failed:" << error;
        return 2;
    }

    emitTelemetry("worker.finished", {
        {"status", "ok"},
        {"activitiesStarted", activityStartedCount},
        {"activitiesFinished", activityFinishedCount},
        {"activityRetries", activityRetryCount},
        {"activityFailed", activityFailedCount},
        {"activitySkipped", activitySkippedCount},
        {"compensationStarted", compensationStartedCount},
        {"compensationFinished", compensationFinishedCount},
        {"compensationFailed", compensationFailedCount},
        {"parallelBatchStarted", parallelBatchStartedCount},
        {"parallelBatchFinished", parallelBatchFinishedCount},
        {"analyzerStarted", analyzerStartedCount},
        {"analyzerFinished", analyzerFinishedCount},
        {"reporterStarted", reporterStartedCount},
        {"reporterFinished", reporterFinishedCount}
    });
    qInfo() << "MVP completed.";
    return 0;
}
