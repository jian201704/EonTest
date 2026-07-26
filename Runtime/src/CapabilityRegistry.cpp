#include "eon/sdk/CapabilityRegistry.h"

#include <QJsonArray>

namespace eon::sdk {

void CapabilityRegistry::registerPlugin(
    const std::string& pluginId, const std::string& version,
    const std::vector<std::string>& capabilities,
    const std::vector<std::string>& resourceTypes)
{
    std::lock_guard lock(mutex_);

    PluginInfo info;
    info.version = version;
    info.capabilities.insert(capabilities.begin(), capabilities.end());
    info.resourceTypes.insert(resourceTypes.begin(), resourceTypes.end());

    plugins_[pluginId] = std::move(info);

    // 更新能力反向索引
    for (const auto& cap : capabilities) {
        capabilityIndex_[cap].push_back(pluginId);
    }

    // 更新资源类型反向索引
    for (const auto& rt : resourceTypes) {
        resourceTypeIndex_[rt].push_back(pluginId);
    }
}

void CapabilityRegistry::registerFromMetadata(const std::string& pluginId,
                                               const QJsonObject& metadata)
{
    QString ver = metadata.value("version").toString();
    std::vector<std::string> capabilities;
    std::vector<std::string> resourceTypes;

    // 读取 capabilities 数组
    QJsonArray caps = metadata.value("capabilities").toArray();
    for (const auto& c : caps) {
        capabilities.push_back(c.toString().toStdString());
    }

    // 读取 resourceTypes 数组
    QJsonArray types = metadata.value("resourceTypes").toArray();
    for (const auto& t : types) {
        resourceTypes.push_back(t.toString().toStdString());
    }

    registerPlugin(pluginId, ver.toStdString(), capabilities, resourceTypes);
}

std::vector<std::string> CapabilityRegistry::findPluginsByCapability(
    const std::string& capability) const
{
    std::lock_guard lock(mutex_);
    auto it = capabilityIndex_.find(capability);
    if (it != capabilityIndex_.end()) return it->second;
    return {};
}

std::vector<std::string> CapabilityRegistry::findPluginsByResourceType(
    const std::string& resourceType) const
{
    std::lock_guard lock(mutex_);
    auto it = resourceTypeIndex_.find(resourceType);
    if (it != resourceTypeIndex_.end()) return it->second;
    return {};
}

std::vector<std::string> CapabilityRegistry::pluginCapabilities(
    const std::string& pluginId) const
{
    std::lock_guard lock(mutex_);
    auto it = plugins_.find(pluginId);
    if (it == plugins_.end()) return {};
    return {it->second.capabilities.begin(), it->second.capabilities.end()};
}

std::string CapabilityRegistry::pluginVersion(const std::string& pluginId) const
{
    std::lock_guard lock(mutex_);
    auto it = plugins_.find(pluginId);
    if (it == plugins_.end()) return {};
    return it->second.version;
}

std::vector<std::string> CapabilityRegistry::allPlugins() const
{
    std::lock_guard lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(plugins_.size());
    for (const auto& [id, _] : plugins_) {
        ids.push_back(id);
    }
    return ids;
}

void CapabilityRegistry::clear()
{
    std::lock_guard lock(mutex_);
    plugins_.clear();
    capabilityIndex_.clear();
    resourceTypeIndex_.clear();
}

bool CapabilityRegistry::hasCapability(const std::string& pluginId,
                                        const std::string& capability) const
{
    std::lock_guard lock(mutex_);
    auto it = plugins_.find(pluginId);
    if (it == plugins_.end()) return false;
    return it->second.capabilities.count(capability) > 0;
}

} // namespace eon::sdk
