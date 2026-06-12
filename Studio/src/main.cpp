#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

namespace {

struct StudioLaunchConfig {
    QString pluginDirectory;
    QStringList workflowFilePaths;
    int cellCount = 1;
    bool batchMode = false;
};

class TelemetryCounters {
public:
    void reset() {
        eventCount_.clear();
    }

    void count(const QString& eventName) {
        if (!eventName.isEmpty()) {
            eventCount_[eventName] = eventCount_.value(eventName, 0) + 1;
        }
    }

    int value(const QString& eventName) const {
        return eventCount_.value(eventName, 0);
    }

    QString toSummaryText() const {
        QStringList lines;
        lines
            << QString("workflow: started=%1 finished=%2 failed=%3")
                   .arg(value("workflow.started"))
                   .arg(value("workflow.finished"))
                   .arg(value("workflow.failed"))
            << QString("activity: started=%1 finished=%2 retry=%3 failed=%4 skipped=%5")
                   .arg(value("activity.started"))
                   .arg(value("activity.finished"))
                   .arg(value("activity.retry"))
                   .arg(value("activity.failed"))
                   .arg(value("activity.skipped"))
            << QString("parallel: batchStarted=%1 batchFinished=%2")
                   .arg(value("parallel.batch.started"))
                   .arg(value("parallel.batch.finished"))
            << QString("compensation: started=%1 finished=%2 failed=%3")
                   .arg(value("compensation.started"))
                   .arg(value("compensation.finished"))
                   .arg(value("compensation.failed"))
            << QString("post: analyzer(started=%1 finished=%2) reporter(started=%3 finished=%4)")
                   .arg(value("analyzer.started"))
                   .arg(value("analyzer.finished"))
                   .arg(value("reporter.started"))
                   .arg(value("reporter.finished"))
            << QString("scheduler: taskStarted=%1 taskFinished=%2")
                   .arg(value("orchestration.task.started"))
                   .arg(value("orchestration.task.finished"));
        return lines.join('\n');
    }

private:
    QHash<QString, int> eventCount_;
};

void emitStudioEvent(const QString& eventName, const QJsonObject& fields = {}) {
    QJsonObject payload{
        {"source", "studio"},
        {"event", eventName},
        {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
    };
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        payload.insert(it.key(), it.value());
    }
    qInfo().noquote() << QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

QString orchestratorFileName() {
#if defined(Q_OS_WIN)
    return "eon-orchestrator.exe";
#else
    return "eon-orchestrator";
#endif
}

QStringList buildOrchestratorArgs(
    const QString& pluginDirectory,
    const QStringList& workflows,
    int cellCount
) {
    QStringList args;
    if (cellCount > 1) {
        args << "--cells" << QString::number(cellCount);
    }
    args << pluginDirectory;
    args.append(workflows);
    return args;
}

StudioLaunchConfig parseArgs(int argc, char* argv[], const QString& defaultPluginDirectory) {
    StudioLaunchConfig config;
    config.pluginDirectory = defaultPluginDirectory;

    bool pluginProvided = false;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == "--cells") {
            if (i + 1 >= argc) {
                continue;
            }
            bool ok = false;
            const int value = QString::fromLocal8Bit(argv[++i]).toInt(&ok);
            if (ok && value > 0) {
                config.cellCount = value;
            }
            continue;
        }
        if (argument == "--batch") {
            config.batchMode = true;
            continue;
        }

        if (!pluginProvided) {
            config.pluginDirectory = argument;
            pluginProvided = true;
        } else {
            config.workflowFilePaths.append(argument);
        }
    }

    return config;
}

void parseTelemetryLine(const QString& line, TelemetryCounters* counters) {
    if (counters == nullptr) {
        return;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }
    counters->count(doc.object().value("event").toString());
}

int runBatchMode(const QString& orchestratorPath, const StudioLaunchConfig& config) {
    emitStudioEvent("monitor.started", {
        {"orchestratorPath", orchestratorPath},
        {"pluginDirectory", config.pluginDirectory},
        {"workflowFilePaths", QJsonArray::fromStringList(config.workflowFilePaths)},
        {"cellCount", config.cellCount}
    });

    QProcess orchestrator;
    orchestrator.setProgram(orchestratorPath);
    orchestrator.setArguments(buildOrchestratorArgs(
        config.pluginDirectory,
        config.workflowFilePaths,
        config.cellCount
    ));
    orchestrator.setProcessChannelMode(QProcess::MergedChannels);
    orchestrator.start();
    if (!orchestrator.waitForStarted()) {
        qCritical() << "Failed to start orchestrator.";
        return 21;
    }

    TelemetryCounters counters;
    QString pendingBuffer;
    while (orchestrator.state() != QProcess::NotRunning) {
        orchestrator.waitForReadyRead(100);
        pendingBuffer += QString::fromLocal8Bit(orchestrator.readAllStandardOutput());
        while (true) {
            const int newLinePos = pendingBuffer.indexOf('\n');
            if (newLinePos < 0) {
                break;
            }
            const QString line = pendingBuffer.left(newLinePos).trimmed();
            pendingBuffer.remove(0, newLinePos + 1);
            if (line.isEmpty()) {
                continue;
            }
            qInfo().noquote() << line;
            parseTelemetryLine(line, &counters);
        }
    }

    const QString trailingLine = pendingBuffer.trimmed();
    if (!trailingLine.isEmpty()) {
        qInfo().noquote() << trailingLine;
        parseTelemetryLine(trailingLine, &counters);
    }

    orchestrator.waitForFinished(-1);
    const int exitCode = orchestrator.exitCode();
    emitStudioEvent("monitor.summary", {
        {"orchestratorExitCode", exitCode},
        {"summary", counters.toSummaryText()}
    });
    return exitCode;
}

class StudioWindow final : public QWidget {
    Q_OBJECT

public:
    StudioWindow(const QString& orchestratorPath, const StudioLaunchConfig& config, QWidget* parent = nullptr)
        : QWidget(parent)
        , orchestratorPath_(orchestratorPath)
        , process_(new QProcess(this)) {
        setWindowTitle("Eon Studio - MVP Monitor");
        resize(1080, 700);

        pluginPathEdit_ = new QLineEdit(config.pluginDirectory, this);
        auto* browsePluginButton = new QPushButton("Browse...", this);
        connect(browsePluginButton, &QPushButton::clicked, this, [this]() {
            const QString dir = QFileDialog::getExistingDirectory(
                this,
                "Select Plugin Directory",
                pluginPathEdit_->text()
            );
            if (!dir.isEmpty()) {
                pluginPathEdit_->setText(dir);
            }
        });

        cellsSpin_ = new QSpinBox(this);
        cellsSpin_->setRange(1, 16);
        cellsSpin_->setValue(config.cellCount);

        workflowList_ = new QListWidget(this);
        workflowList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        loadWorkflowCandidates(config.workflowFilePaths);

        auto* runButton = new QPushButton("Run", this);
        auto* stopButton = new QPushButton("Stop", this);
        stopButton->setEnabled(false);

        statusLabel_ = new QLabel("Idle", this);
        summaryLabel_ = new QLabel(this);
        summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        summaryLabel_->setStyleSheet("QLabel { font-family: Consolas, 'Courier New', monospace; }");
        summaryLabel_->setText(counters_.toSummaryText());

        logView_ = new QTextEdit(this);
        logView_->setReadOnly(true);
        logView_->setLineWrapMode(QTextEdit::NoWrap);

        auto* topForm = new QFormLayout();
        auto* pluginRow = new QHBoxLayout();
        pluginRow->addWidget(pluginPathEdit_);
        pluginRow->addWidget(browsePluginButton);
        topForm->addRow("Plugin Directory", pluginRow);
        topForm->addRow("Cells", cellsSpin_);

        auto* workflowBox = new QGroupBox("Workflows", this);
        auto* workflowLayout = new QVBoxLayout(workflowBox);
        workflowLayout->addWidget(workflowList_);

        auto* actionRow = new QHBoxLayout();
        actionRow->addWidget(runButton);
        actionRow->addWidget(stopButton);
        actionRow->addWidget(statusLabel_);
        actionRow->addStretch();

        auto* summaryBox = new QGroupBox("Telemetry Summary", this);
        auto* summaryLayout = new QVBoxLayout(summaryBox);
        summaryLayout->addWidget(summaryLabel_);

        auto* root = new QVBoxLayout(this);
        root->addLayout(topForm);
        root->addWidget(workflowBox, 1);
        root->addLayout(actionRow);
        root->addWidget(summaryBox);
        root->addWidget(logView_, 2);

        connect(runButton, &QPushButton::clicked, this, [this, runButton, stopButton]() {
            startOrchestration();
            runButton->setEnabled(false);
            stopButton->setEnabled(true);
        });
        connect(stopButton, &QPushButton::clicked, this, [this, runButton, stopButton]() {
            if (process_->state() != QProcess::NotRunning) {
                process_->terminate();
                if (!process_->waitForFinished(1500)) {
                    process_->kill();
                }
            }
            runButton->setEnabled(true);
            stopButton->setEnabled(false);
            statusLabel_->setText("Stopped");
        });

        connect(process_, &QProcess::readyReadStandardOutput, this, [this]() {
            pendingOutput_ += QString::fromLocal8Bit(process_->readAllStandardOutput());
            consumeOutputLines();
        });
        connect(process_, &QProcess::finished, this, [this, runButton, stopButton](int exitCode, QProcess::ExitStatus status) {
            consumeOutputLines();
            statusLabel_->setText(
                status == QProcess::NormalExit && exitCode == 0
                    ? "Completed"
                    : QString("Failed (exit %1)").arg(exitCode)
            );
            runButton->setEnabled(true);
            stopButton->setEnabled(false);
        });
    }

private:
    void loadWorkflowCandidates(const QStringList& presetWorkflows) {
        const QString workflowsDir = QDir(QCoreApplication::applicationDirPath()).filePath("../Workflows");
        QStringList candidates = presetWorkflows;
        QDir dir(workflowsDir);
        if (dir.exists()) {
            const QStringList files = dir.entryList(QStringList() << "*.json", QDir::Files);
            for (const QString& fileName : files) {
                candidates.append(dir.absoluteFilePath(fileName));
            }
        }
        candidates.removeDuplicates();
        candidates.sort();

        for (const QString& path : candidates) {
            auto* item = new QListWidgetItem(path, workflowList_);
            if (presetWorkflows.contains(path) || presetWorkflows.isEmpty()) {
                item->setSelected(true);
            }
        }
    }

    QStringList selectedWorkflows() const {
        QStringList workflows;
        const auto items = workflowList_->selectedItems();
        for (QListWidgetItem* item : items) {
            workflows.append(item->text());
        }
        return workflows;
    }

    void appendLogLine(const QString& line) {
        if (line.isEmpty()) {
            return;
        }
        logView_->append(line);
        parseTelemetryLine(line, &counters_);
        summaryLabel_->setText(counters_.toSummaryText());
    }

    void consumeOutputLines() {
        while (true) {
            const int newLinePos = pendingOutput_.indexOf('\n');
            if (newLinePos < 0) {
                break;
            }
            const QString line = pendingOutput_.left(newLinePos).trimmed();
            pendingOutput_.remove(0, newLinePos + 1);
            appendLogLine(line);
        }
    }

    void startOrchestration() {
        counters_.reset();
        summaryLabel_->setText(counters_.toSummaryText());
        logView_->clear();
        pendingOutput_.clear();

        const QStringList workflows = selectedWorkflows();
        const QStringList args = buildOrchestratorArgs(
            pluginPathEdit_->text().trimmed(),
            workflows,
            cellsSpin_->value()
        );

        emitStudioEvent("monitor.started", {
            {"orchestratorPath", orchestratorPath_},
            {"pluginDirectory", pluginPathEdit_->text().trimmed()},
            {"workflowFilePaths", QJsonArray::fromStringList(workflows)},
            {"cellCount", cellsSpin_->value()}
        });

        statusLabel_->setText("Running...");
        process_->setProgram(orchestratorPath_);
        process_->setArguments(args);
        process_->setProcessChannelMode(QProcess::MergedChannels);
        process_->start();
        if (!process_->waitForStarted(3000)) {
            statusLabel_->setText("Failed to start orchestrator");
        }
    }

    QString orchestratorPath_;
    QProcess* process_;
    QLineEdit* pluginPathEdit_ = nullptr;
    QSpinBox* cellsSpin_ = nullptr;
    QListWidget* workflowList_ = nullptr;
    QTextEdit* logView_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    TelemetryCounters counters_;
    QString pendingOutput_;
};

} // namespace

int main(int argc, char* argv[]) {
    const QString executableDirectory = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
    const QString defaultPluginDirectory = QDir(executableDirectory).filePath("../Plugins");
    const StudioLaunchConfig config = parseArgs(argc, argv, defaultPluginDirectory);

    const QString orchestratorPath = QDir(executableDirectory).filePath(orchestratorFileName());
    if (!QFileInfo::exists(orchestratorPath)) {
        qCritical() << "Orchestrator not found:" << orchestratorPath;
        return 20;
    }

    if (config.batchMode) {
        QCoreApplication app(argc, argv);
        return runBatchMode(orchestratorPath, config);
    }

    QApplication app(argc, argv);
    StudioWindow window(orchestratorPath, config);
    window.show();
    return app.exec();
}

#include "main.moc"
