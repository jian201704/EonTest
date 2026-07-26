#include "eon/runtime/ResourceDependencyAnalyzer.h"

#include <algorithm>
#include <queue>
#include <sstream>
#include <stack>

namespace eon::runtime {

// ============================================================
// 递归构建依赖树（参考 OpenTAP GetResourceTree）
// ============================================================
void ResourceDependencyAnalyzer::buildTree(
    const std::string& resourceId,
    std::function<eon::sdk::IResource*(const std::string&)> resourceLookup,
    std::unordered_set<std::string>& visited,
    std::vector<ResourceNode>& nodes)
{
    if (visited.find(resourceId) != visited.end()) return;
    visited.insert(resourceId);

    auto* resource = resourceLookup(resourceId);
    if (!resource) return;

    ResourceNode node;
    node.resourceId = resourceId;
    node.resource = resource;

    // 递归扫描此资源的依赖
    auto deps = resource->dependencies();
    for (auto* dep : deps) {
        if (!dep) continue;

        // 查找依赖对应的 resourceId
        // 这里简化处理：用 dep->name() 作为 ID
        // 更精确的方式是维护反向映射表，但 name 通常就是注册 ID
        std::string depId = dep->name().toStdString();

        // 递归分析依赖的资源
        buildTree(depId, resourceLookup, visited, nodes);

        // 当前资源依赖于 dep
        node.strongDependencies.push_back(depId);
    }

    nodes.push_back(std::move(node));
}

// ============================================================
// 传递闭包扩展（参考 OpenTAP ExpandTree）
// A → B → C  =>  A 也依赖于 C
// ============================================================
void ResourceDependencyAnalyzer::expandTree(std::vector<ResourceNode>& nodes)
{
    // 建立 resourceId → index 查找表
    std::unordered_map<std::string, size_t> indexMap;
    for (size_t i = 0; i < nodes.size(); ++i) {
        indexMap[nodes[i].resourceId] = i;
    }

    bool changed = false;
    do {
        changed = false;
        for (auto& node : nodes) {
            // 收集所有间接依赖
            std::vector<std::string> transitiveDeps;
            for (const auto& depId : node.strongDependencies) {
                auto it = indexMap.find(depId);
                if (it == indexMap.end()) continue;
                const auto& depNode = nodes[it->second];
                // 把依赖的依赖加进来
                for (const auto& transDep : depNode.strongDependencies) {
                    // 避免重复
                    if (std::find(node.strongDependencies.begin(),
                                  node.strongDependencies.end(),
                                  transDep) == node.strongDependencies.end()) {
                        transitiveDeps.push_back(transDep);
                    }
                }
            }
            if (!transitiveDeps.empty()) {
                node.strongDependencies.insert(
                    node.strongDependencies.end(),
                    transitiveDeps.begin(), transitiveDeps.end());
                changed = true;
            }
        }
    } while (changed);
}

// ============================================================
// Tarjan SCC — 检测循环依赖
// ============================================================
void ResourceDependencyAnalyzer::strongConnect(
    const ResourceNode* node,
    std::unordered_map<const ResourceNode*, TarjanVertex>& vertices,
    std::vector<const ResourceNode*>& stack,
    std::vector<std::vector<const ResourceNode*>>& sccs,
    int& index)
{
    auto& v = vertices[node];
    v.index = index;
    v.lowlink = index;
    index++;
    stack.push_back(node);
    v.onStack = true;

    // 遍历所有强依赖边
    for (const auto& depId : node->strongDependencies) {
        // 找到依赖对应的 ResourceNode*
        const ResourceNode* wNode = nullptr;
        for (const auto& [key, val] : vertices) {
            if (key->resourceId == depId) {
                wNode = key;
                break;
            }
        }
        if (!wNode) continue;

        auto& w = vertices[wNode];
        if (w.index == -1) {
            strongConnect(wNode, vertices, stack, sccs, index);
            v.lowlink = std::min(v.lowlink, w.lowlink);
        } else if (w.onStack) {
            v.lowlink = std::min(v.lowlink, w.index);
        }
    }

    if (v.lowlink == v.index) {
        std::vector<const ResourceNode*> scc;
        const ResourceNode* w;
        do {
            w = stack.back();
            stack.pop_back();
            vertices[w].onStack = false;
            scc.push_back(w);
        } while (w != node);

        if (scc.size() > 1) {
            sccs.push_back(scc);
        }
    }
}

void ResourceDependencyAnalyzer::tarjanSCC(const std::vector<ResourceNode>& nodes)
{
    std::unordered_map<const ResourceNode*, TarjanVertex> vertices;
    for (const auto& node : nodes) {
        vertices[&node] = TarjanVertex();
    }

    std::vector<const ResourceNode*> stack;
    std::vector<std::vector<const ResourceNode*>> sccs;
    int index = 0;

    for (const auto& [node, vertex] : vertices) {
        if (vertex.index == -1) {
            strongConnect(node, vertices, stack, sccs, index);
        }
    }

    if (!sccs.empty()) {
        hasCycle_ = true;
        std::ostringstream oss;
        oss << "Circular dependencies detected: ";
        for (size_t i = 0; i < sccs.size(); ++i) {
            if (i > 0) oss << "; ";
            oss << "[";
            for (size_t j = 0; j < sccs[i].size(); ++j) {
                if (j > 0) oss << " -> ";
                oss << sccs[i][j]->resourceId;
            }
            oss << "]";
        }
        cycleDescription_ = oss.str();
    }
}

// ============================================================
// 主入口：analyze
// ============================================================
std::vector<ResourceNode> ResourceDependencyAnalyzer::analyze(
    const std::vector<std::string>& resourceIds,
    std::function<eon::sdk::IResource*(const std::string&)> resourceLookup,
    std::string* errorMessage)
{
    hasCycle_ = false;
    cycleDescription_.clear();

    // 1. 构建依赖树
    std::vector<ResourceNode> nodes;
    std::unordered_set<std::string> visited;
    for (const auto& rid : resourceIds) {
        buildTree(rid, resourceLookup, visited, nodes);
    }

    // 2. 传递闭包扩展
    expandTree(nodes);

    // 3. 自引用检查
    for (const auto& node : nodes) {
        for (const auto& dep : node.strongDependencies) {
            if (dep == node.resourceId) {
                if (errorMessage) {
                    *errorMessage = "Resource '" + node.resourceId + "' references itself.";
                }
                return nodes;
            }
        }
    }

    // 4. 循环依赖检测（Tarjan SCC）
    tarjanSCC(nodes);
    if (hasCycle_ && errorMessage) {
        *errorMessage = cycleDescription_;
    }

    // 5. 拓扑排序（Kahn 算法）
    // 先计算入度
    std::unordered_map<std::string, int> inDegree;
    for (const auto& node : nodes) {
        if (inDegree.find(node.resourceId) == inDegree.end()) {
            inDegree[node.resourceId] = 0;
        }
    }
    for (const auto& node : nodes) {
        for (const auto& dep : node.strongDependencies) {
            inDegree[dep]++; // 被依赖的资源入度增加
        }
    }

    // Kahn 算法：入度为 0 的先出
    // 注意：这里的逻辑是"依赖先打开"，所以被依赖的入度应该为 0 先输出
    // 重新计算：被依赖者入度=0（没有更前置的依赖了）
    std::unordered_map<std::string, int> outDegree;
    for (const auto& node : nodes) {
        if (outDegree.find(node.resourceId) == outDegree.end()) {
            outDegree[node.resourceId] = 0;
        }
    }
    for (const auto& node : nodes) {
        for (const auto& dep : node.strongDependencies) {
            auto it = outDegree.find(dep);
            if (it != outDegree.end()) {
                // dep 被 node 依赖，所以 dep 的出度++（dep 有后续节点）
            }
        }
    }

    // 正确拓扑排序：被依赖的在前
    // 入度 = 依赖此资源的其他资源数（被依赖越多入度越高）
    // 我们要找"没有依赖别人的"先排
    std::unordered_map<std::string, int> depCount; // 每个资源依赖多少个别人
    for (const auto& node : nodes) {
        depCount[node.resourceId] = static_cast<int>(node.strongDependencies.size());
    }

    std::queue<std::string> q;
    for (const auto& [rid, count] : depCount) {
        if (count == 0) {
            q.push(rid);
        }
    }

    std::vector<std::string> sorted;
    while (!q.empty()) {
        auto rid = q.front();
        q.pop();
        sorted.push_back(rid);

        // 找到依赖此资源的节点，减少它们的计数
        for (auto& node : nodes) {
            auto it = std::find(node.strongDependencies.begin(),
                                node.strongDependencies.end(), rid);
            if (it != node.strongDependencies.end()) {
                depCount[node.resourceId]--;
                if (depCount[node.resourceId] == 0) {
                    q.push(node.resourceId);
                }
            }
        }
    }

    // 如果有环，sorted 可能比 nodes 少
    // 此时直接返回原顺序
    if (sorted.size() != nodes.size()) {
        return nodes;
    }

    // 按拓扑序重排 nodes
    std::vector<ResourceNode> result;
    result.reserve(nodes.size());
    for (const auto& rid : sorted) {
        for (const auto& node : nodes) {
            if (node.resourceId == rid) {
                result.push_back(node);
                break;
            }
        }
    }

    return result;
}

} // namespace eon::runtime
