#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace eon::sdk {

/// <summary>
/// 插件能力注册表（章节 9）。
/// 引擎在启动时读取插件 manifest，构建 capability 索引，
/// 调度前匹配 step 所需 capability。
/// </summary>
class CapabilityRegistry {
public:
    CapabilityRegistry() = default;

    /// 注册插件能力
    void registerPlugin(const std::string& pluginId, const std::string& version,
                         const std::vector<std::string>& capabilities,
                         const std::vector<std::string>& resourceTypes);

    /// 从 QPluginLoader 的 MetaData JSON 注册
    void registerFromMetadata(const std::string& pluginId, const QJsonObject& metadata);

    /// 查询拥有指定能力的所有插件
    std::vector<std::string> findPluginsByCapability(const std::string& capability) const;

    /// 查询支持指定资源类型的所有插件
    std::vector<std::string> findPluginsByResourceType(const std::string& resourceType) const;

    /// 查询插件的所有能力
    std::vector<std::string> pluginCapabilities(const std::string& pluginId) const;

    /// 查询插件的版本
    std::string pluginVersion(const std::string& pluginId) const;

    /// 获取所有已注册的插件 ID
    std::vector<std::string> allPlugins() const;

    /// 清除所有注册
    void clear();

    /// 检查插件是否具备指定能力
    bool hasCapability(const std::string& pluginId, const std::string& capability) const;

private:
    struct PluginInfo {
        std::string version;
        std::unordered_set<std::string> capabilities;
        std::unordered_set<std::string> resourceTypes;
    };

    std::unordered_map<std::string, PluginInfo> plugins_;
    // 能力 -> 插件列表 反向索引
    std::unordered_map<std::string, std::vector<std::string>> capabilityIndex_;
    // 资源类型 -> 插件列表 反向索引
    std::unordered_map<std::string, std::vector<std::string>> resourceTypeIndex_;

    mutable std::mutex mutex_;
};

} // namespace eon::sdk
