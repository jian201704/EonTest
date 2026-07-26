#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "eon/sdk/IResource.h"

namespace eon::runtime {

/// <summary>
/// 资源依赖节点（参考 OpenTAP ResourceNode）。
/// 表示一个资源及其在依赖树中的位置。
/// </summary>
struct ResourceNode {
    std::string resourceId;
    eon::sdk::IResource* resource = nullptr;

    /// 强依赖：这些资源必须在此资源之前打开
    std::vector<std::string> strongDependencies;

    /// 弱依赖：这些资源可以与此资源并行打开
    std::vector<std::string> weakDependencies;
};

/// <summary>
/// 资源依赖分析器（参考 OpenTAP ResourceDependencyAnalyzer）。
///
/// 功能：
/// 1. 递归扫描资源的 dependencies() 构建依赖图
/// 2. 拓扑排序 — 确保资源按依赖顺序打开
/// 3. Tarjan 算法检测循环依赖
/// 4. 传递闭包扩展 — A→B→C 则 A 也依赖 C
///
/// 使用示例：
///   ResourceDependencyAnalyzer analyzer;
///   auto nodes = analyzer.analyze(resources, idMap);
///   if (analyzer.hasCircularDependency(nodes)) { /* 报错 */ }
///   // nodes 按拓扑序排列，可直接用 preallocateAll 按序打开
/// </summary>
class ResourceDependencyAnalyzer {
public:
    ResourceDependencyAnalyzer() = default;

    /// <summary>
    /// 分析资源列表，构建依赖关系图。
    /// @param resourceIds 注册到 ResourceManager 的资源 ID 列表
    /// @param resourceLookup 根据 resourceId 查找 IResource* 的函数
    /// @return 按拓扑序排列的节点列表（依赖在前，被依赖在后）
    ///         如果存在循环依赖，返回的 errorMessage 非空
    /// </summary>
    std::vector<ResourceNode> analyze(
        const std::vector<std::string>& resourceIds,
        std::function<eon::sdk::IResource*(const std::string&)> resourceLookup,
        std::string* errorMessage = nullptr);

    /// <summary>
    /// 检测是否存在循环依赖（在 analyze 之后调用）
    /// </summary>
    bool hasCircularDependency() const { return hasCycle_; }

    /// <summary>
    /// 获取循环依赖的描述
    /// </summary>
    std::string circularDependencyDescription() const { return cycleDescription_; }

private:
    // Tarjan SCC 算法内部结构
    struct TarjanVertex {
        int index = -1;
        int lowlink = -1;
        bool onStack = false;
    };

    // 递归构建依赖树（对应 OpenTAP GetResourceTree）
    void buildTree(const std::string& resourceId,
                   std::function<eon::sdk::IResource*(const std::string&)> resourceLookup,
                   std::unordered_set<std::string>& visited,
                   std::vector<ResourceNode>& nodes);

    // 传递闭包扩展（对应 OpenTAP ExpandTree）
    void expandTree(std::vector<ResourceNode>& nodes);

    // Tarjan SCC — 检测循环依赖
    void tarjanSCC(const std::vector<ResourceNode>& nodes);

    void strongConnect(const ResourceNode* node,
                       std::unordered_map<const ResourceNode*, TarjanVertex>& vertices,
                       std::vector<const ResourceNode*>& stack,
                       std::vector<std::vector<const ResourceNode*>>& sccs,
                       int& index);

    bool hasCycle_ = false;
    std::string cycleDescription_;
};

} // namespace eon::runtime
