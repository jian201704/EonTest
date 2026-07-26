#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "eon/studio/StudioBackend.h"
#include "eon/studio/WorkflowEditorModel.h"
#include "eon/studio/BenchSettings.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Eon Studio");
    app.setApplicationVersion("0.2.0");
    app.setOrganizationName("EonTest");

    QQuickStyle::setStyle("Material");

    QCommandLineParser parser;
    parser.setApplicationDescription("Eon Studio");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"batch", "CLI batch mode, exit on completion."});
    parser.addOption({"cells", "Number of CELLs", "count", "1"});
    parser.addOption({"state", "State DB path", "path"});
    parser.addOption({"resume", "Resume from state DB."});
    parser.addOption({"stop-on-failure", "Stop on first failure."});
    parser.addPositionalArgument("plugins", "Plugin directory");
    parser.addPositionalArgument("workflows", "Workflow file(s)");
    parser.process(app);

    auto* backend = new eon::studio::StudioBackend(&app);
    if (parser.isSet("batch")) backend->login("admin", "admin");
    auto* editorModel = new eon::studio::WorkflowEditorModel(&app);
    auto* benchSettings = new eon::studio::BenchSettings(&app);
    benchSettings->load();  // 启动时加载当前 profile
    editorModel->setAvailablePlugins({"sample.activity", "sample.analyzer", "sample.reporter",
                                       "can.send", "can.receive", "uds.readDID", "uds.writeDID",
                                       "serial.send", "gpio.set", "delay", "measure.voltage",
                                       "power.supply", "voltage.analyzer",
                                       "eon.reporter.csv", "eon.reporter.json", "eon.reporter.mqtt"});

    if (parser.isSet("cells")) backend->setCellCount(parser.value("cells").toInt());
    if (parser.isSet("state")) backend->setStateFilePath(parser.value("state"));
    if (parser.isSet("stop-on-failure")) backend->setStopOnFailure(true);

    const QStringList pos = parser.positionalArguments();
    if (!pos.isEmpty()) {
        backend->setPluginDirectory(pos.first());
        if (pos.size() > 1) backend->setSelectedWorkflows(pos.mid(1));
    }

    if (parser.isSet("batch")) {
        qInfo() << "Eon Studio - Batch Mode";
        if (parser.isSet("resume")) backend->resumeFromState();
        else backend->runSelected();
        QObject::connect(backend, &eon::studio::StudioBackend::runningChanged,
                         &app, [&]() {
            if (!backend->isRunning()) { qInfo() << "Batch complete."; app.quit(); }
        });
        return app.exec();
    }

    QQmlApplicationEngine engine;
    qmlRegisterUncreatableType<eon::studio::StudioBackend>(
        "Eon.Studio", 1, 0, "StudioBackend", "Created in C++");
    engine.rootContext()->setContextProperty("backend", backend);
    engine.rootContext()->setContextProperty("editorModel", editorModel);
    engine.rootContext()->setContextProperty("benchSettingsCtx", benchSettings);

    if (parser.isSet("resume")) {
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                         backend, [backend](QObject*, const QUrl&) {
            backend->resumeFromState();
        }, Qt::QueuedConnection);
    }

    engine.load("qrc:/eon/studio/qml/MainWindow.qml");
    if (engine.rootObjects().isEmpty()) return -1;
    return app.exec();
}
