#include "eon/runtime/PluginManager.h"

#include "eon/sdk/IStepPlugin.h"
#include "eon/sdk/IDut.h"
#include "eon/sdk/Semver.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

constexpr const char* kSupportedContractVersion = "1.0";

QString pluginMetaString(const QJsonObject& metadata, const char* key) {
    return metadata.value(QString::fromLatin1(key)).toString();
}

} // namespace

namespace eon::runtime {

PluginManager::PluginManager() = default;
PluginManager::~PluginManager() = default;

bool PluginManager::loadPlugins(const QString& pluginDirectory, QString* errorMessage) {
    clear();

    const QDir directory(pluginDirectory);
    if (!directory.exists()) {
        if (errorMessage) *errorMessage = QString("Plugin directory does not exist: %1").arg(pluginDirectory);
        return false;
    }

    const QFileInfoList files = directory.entryInfoList(QDir::Files);
    for (const QFileInfo& file : files) {
        auto loader = std::make_unique<QPluginLoader>(file.absoluteFilePath());
        QObject* instance = loader->instance();
        if (!instance) continue;

        const QJsonObject metadata = loader->metaData().value("MetaData").toObject();
        QString manifestError;
        if (!checkManifestCompatibility(metadata, &manifestError)) continue;

        QString pluginId = metadata.value("pluginId").toString();
        QString contractType;

        if (auto* stepPlugin = qobject_cast<eon::sdk::IStepPlugin*>(instance)) {
            contractType = "step";
            pluginId = stepPlugin->id();
            if (stepPluginsById_.contains(pluginId)) continue;
            stepPluginsById_.insert(pluginId, stepPlugin);
        } else if (auto* analyzerPlugin = qobject_cast<eon::sdk::IAnalyzerPlugin*>(instance)) {
            contractType = "analyzer";
            pluginId = analyzerPlugin->id();
            if (analyzerPluginsById_.contains(pluginId)) continue;
            analyzerPluginsById_.insert(pluginId, analyzerPlugin);
        } else if (auto* reporterPlugin = qobject_cast<eon::sdk::IReporterPlugin*>(instance)) {
            contractType = "reporter";
            pluginId = reporterPlugin->id();
            if (reporterPluginsById_.contains(pluginId)) continue;
            reporterPluginsById_.insert(pluginId, reporterPlugin);
        } else if (auto* dutPlugin = qobject_cast<eon::sdk::IDut*>(instance)) {
            contractType = "dut";
            pluginId = dutPlugin->dutId();
            if (dutPluginsById_.contains(pluginId)) continue;
            dutPluginsById_.insert(pluginId, dutPlugin);
        } else {
            continue;
        }

        const QString declaredContractType = pluginMetaString(metadata, "contractType");
        const QString declaredContractVersion = pluginMetaString(metadata, "contractVersion");
        if (declaredContractType != contractType || declaredContractVersion != kSupportedContractVersion) {
            if (contractType == "step") {
                stepPluginsById_.remove(pluginId);
            } else if (contractType == "analyzer") {
                analyzerPluginsById_.remove(pluginId);
            } else if (contractType == "reporter") {
                reporterPluginsById_.remove(pluginId);
            } else if (contractType == "dut") {
                dutPluginsById_.remove(pluginId);
            }
            continue;
        }

        const QString declaredPluginId = pluginMetaString(metadata, "pluginId");
        if (!declaredPluginId.isEmpty() && declaredPluginId != pluginId) {
            if (contractType == "step") {
                stepPluginsById_.remove(pluginId);
            } else if (contractType == "analyzer") {
                analyzerPluginsById_.remove(pluginId);
            } else if (contractType == "reporter") {
                reporterPluginsById_.remove(pluginId);
            } else if (contractType == "dut") {
                dutPluginsById_.remove(pluginId);
            }
            continue;
        }

        plugins_.push_back({std::move(loader), pluginId, contractType, declaredContractVersion});
    }

    if (stepPluginsById_.isEmpty()) {
        if (errorMessage) *errorMessage = QString("No compatible step plugins loaded from: %1").arg(pluginDirectory);
        return false;
    }

    return true;
}

eon::sdk::IStepPlugin* PluginManager::findStepPluginById(const QString& pluginId) const {
    return stepPluginsById_.value(pluginId, nullptr);
}

eon::sdk::IAnalyzerPlugin* PluginManager::findAnalyzerPluginById(const QString& pluginId) const {
    return analyzerPluginsById_.value(pluginId, nullptr);
}

eon::sdk::IReporterPlugin* PluginManager::findReporterPluginById(const QString& pluginId) const {
    return reporterPluginsById_.value(pluginId, nullptr);
}

eon::sdk::IDut* PluginManager::findDutPluginById(const QString& pluginId) const {
    return dutPluginsById_.value(pluginId, nullptr);
}

QStringList PluginManager::stepPluginIds() const {
    return QStringList(stepPluginsById_.keys());
}

void PluginManager::clear() {
    plugins_.clear();
    stepPluginsById_.clear();
    analyzerPluginsById_.clear();
    reporterPluginsById_.clear();
    dutPluginsById_.clear();
}

bool PluginManager::checkManifestCompatibility(const QJsonObject& metadata, QString* errorMessage) {
    QString declaredVersion = pluginMetaString(metadata, "contractVersion");
    if (!declaredVersion.isEmpty() && declaredVersion != kSupportedContractVersion) {
        if (errorMessage) *errorMessage = QString("Unsupported contract version '%1' (expected %2)")
            .arg(declaredVersion, kSupportedContractVersion);
        return false;
    }
    QString compatible = pluginMetaString(metadata, "compatibleEonTest");
    if (!compatible.isEmpty()) {
        std::string eonVer(EON_VERSION);
        if (!eon::sdk::Semver::matches(eonVer, compatible.toStdString())) {
            if (errorMessage) *errorMessage = QString("Requires compatibleEonTest '%1', current is '%2'")
                .arg(compatible, QString::fromStdString(eonVer));
            return false;
        }
    }
    QJsonObject deps = metadata.value("dependencies").toObject();
    for (auto it = deps.constBegin(); it != deps.constEnd(); ++it) {
        if (it.value().toString().isEmpty()) {
            if (errorMessage) *errorMessage = QString("Dependency '%1' has empty version range").arg(it.key());
            return false;
        }
    }
    return true;
}

} // namespace eon::runtime
