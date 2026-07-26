#pragma once

namespace eon::sdk {

/// <summary>
/// 资源打开策略（对标 OpenTAP ResourceOpenBehavior）。
/// 控制依赖资源的打开顺序和并行性。
/// </summary>
enum class ResourceOpenPolicy {
    /// 强依赖：依赖资源先 Open，当前资源再 Open（默认）
    Before = 0,
    /// 弱依赖：依赖资源和当前资源并行 Open
    InParallel = 1,
    /// 忽略：不自动 Open 该依赖资源
    Ignore = 2
};

} // namespace eon::sdk
