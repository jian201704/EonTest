#pragma once

#include <QString>

namespace eon::sdk {

/// <summary>
/// 所有插件的基接口。
/// 对标 OpenTAP ITapPlugin（所有插件的标记基接口）。
/// 
/// 所有插件类型（IStepPlugin、IAnalyzerPlugin、IReporterPlugin、IDut 等）
/// 均应继承自 IPlugin，确保整个插件系统有统一的根类型。
/// </summary>
class IPlugin {
public:
    virtual ~IPlugin() = default;

    /// <summary>
    /// 插件唯一标识，如 "sample.activity"。
    /// </summary>
    virtual QString id() const = 0;

    /// <summary>
    /// 插件显示名称（用于 UI 展示）。
    /// 默认返回 id()，子类可重写。
    /// </summary>
    virtual QString displayName() const { return id(); }
};

} // namespace eon::sdk
