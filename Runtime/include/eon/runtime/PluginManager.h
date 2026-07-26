#pragma once

#include <memory>
#include <vector>

#include <QHash>
#include <QJsonObject>
#include <QPluginLoader>
#include <QString>

namespace eon::sdk {
class IStepPlugin;
class IAnalyzerPlugin;
class IReporterPlugin;
class IDut;
} // namespace eon::sdk

namespace eon::runtime {

/// <summary>
/// 插件管理器。
/// 负责插件的加载、卸载、查找、兼容性检查。
/// 从 WorkflowEngine 中解耦，对标 OpenTAP PluginManager。
/// 支持 step/analyzer/reporter/dut 四种插件类型。
/// </summary>
class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    /// <summary>
    /// 从目录加载所有兼容插件。
    /// 自动识别 step/analyzer/reporter/dut 类型并分类注册。
    /// </summary>
    bool loadPlugins(const QString& pluginDirectory, QString* errorMessage = nullptr);

    /// <summary>
    /// 按 ID 查找步骤插件。
    /// </summary>
    eon::sdk::IStepPlugin* findStepPluginById(const QString& pluginId) const;

    /// <summary>
    /// 按 ID 查找分析器插件。
    /// </summary>
    eon::sdk::IAnalyzerPlugin* findAnalyzerPluginById(const QString& pluginId) const;

    /// <summary>
    /// 按 ID 查找报告器插件。
    /// </summary>
    eon::sdk::IReporterPlugin* findReporterPluginById(const QString& pluginId) const;

    /// <summary>
    /// 按 ID 查找 DUT 插件（对标 OpenTAP DutSettings）。
    /// </summary>
    eon::sdk::IDut* findDutPluginById(const QString& pluginId) const;

    /// <summary>
    /// 获取所有已注册的步骤插件 ID 列表。
    /// </summary>
    QStringList stepPluginIds() const;

    /// <summary>
    /// 已加载的步骤插件数量。
    /// </summary>
    int stepPluginCount() const { return stepPluginsById_.size(); }

    /// 所有步骤插件映射（工作流结束生命周期回调用）
    const QHash<QString, eon::sdk::IStepPlugin*>& stepPlugins() const { return stepPluginsById_; }

    /// 所有分析器插件映射（迭代用）
    const QHash<QString, eon::sdk::IAnalyzerPlugin*>& analyzers() const { return analyzerPluginsById_; }

    /// 所有报告器插件映射
    const QHash<QString, eon::sdk::IReporterPlugin*>& reporters() const { return reporterPluginsById_; }

    /// 所有 DUT 插件映射
    const QHash<QString, eon::sdk::IDut*>& duts() const { return dutPluginsById_; }

    /// <summary>
    /// 清空所有已加载的插件。
    /// </summary>
    void clear();

private:
    struct PluginHandle {
        std::unique_ptr<QPluginLoader> loader;
        QString pluginId;
        QString contractType;
        QString contractVersion;
    };

    /// <summary>
    /// 检查插件 manifest 兼容性（contractVersion / compatibleEonTest / dependencies）。
    /// </summary>
    bool checkManifestCompatibility(const QJsonObject& metadata, QString* errorMessage);

    std::vector<PluginHandle> plugins_;
    QHash<QString, eon::sdk::IStepPlugin*> stepPluginsById_;
    QHash<QString, eon::sdk::IAnalyzerPlugin*> analyzerPluginsById_;
    QHash<QString, eon::sdk::IReporterPlugin*> reporterPluginsById_;
    QHash<QString, eon::sdk::IDut*> dutPluginsById_;
};

} // namespace eon::runtime
