#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#include "eon/core/EventBus.h"
#include "eon/runtime/WorkflowEngine.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const QString pluginDirectory = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QDir(QCoreApplication::applicationDirPath()).filePath("../plugins");

    eon::core::EventBus eventBus;
    eventBus.subscribe("workflow.started", [](const QVariantMap& payload) {
        qInfo() << "workflow.started" << payload;
    });
    eventBus.subscribe("activity.started", [](const QVariantMap& payload) {
        qInfo() << "activity.started" << payload;
    });
    eventBus.subscribe("activity.finished", [](const QVariantMap& payload) {
        qInfo() << "activity.finished" << payload;
    });
    eventBus.subscribe("workflow.finished", [](const QVariantMap& payload) {
        qInfo() << "workflow.finished" << payload;
    });
    eventBus.subscribe("workflow.failed", [](const QVariantMap& payload) {
        qWarning() << "workflow.failed" << payload;
    });

    eon::runtime::WorkflowEngine engine(&eventBus);

    QString error;
    if (!engine.loadPlugins(pluginDirectory, &error)) {
        qCritical() << "Load plugins failed:" << error;
        return 1;
    }

    if (!engine.executeMinimalWorkflow(&error)) {
        qCritical() << "Execute workflow failed:" << error;
        return 2;
    }

    qInfo() << "MVP completed.";
    return 0;
}
