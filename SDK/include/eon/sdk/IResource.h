#pragma once

#include <vector>

#include <QString>

#include "eon/sdk/ResourceOpenPolicy.h"

namespace eon::sdk {

/// <summary>
/// Resource 生命周期管理接口。
/// 所有仪器、DUT、结果监听器等资源都应实现此接口。
/// 引擎层（WorkflowEngine/ResourceManager）自动管理 Open/Close。
/// </summary>
class IResource {
public:
    virtual ~IResource() = default;

    /// <summary>
    /// 打开与资源的连接。由引擎在执行前自动调用。
    /// </summary>
    /// <returns>true 表示连接成功。</returns>
    virtual bool open() = 0;

    /// <summary>
    /// 关闭与资源的连接。由引擎在执行结束后自动调用。
    /// </summary>
    virtual void close() = 0;

    /// <summary>
    /// 资源名称（唯一标识，对应 Excel 中的设备名称）。
    /// </summary>
    virtual QString name() const = 0;

    /// <summary>
    /// 连接是否处于打开状态。
    /// </summary>
    virtual bool isConnected() const = 0;

    /// <summary>
    /// 返回此资源依赖的其他资源列表（参考 OpenTAP ResourceDependencyAnalyzer）。
    /// 引擎在打开此资源前，会先打开依赖列表中返回的资源。
    /// 默认返回空（无依赖），子类可重写以声明依赖关系。
    /// 例如：一个仪器资源依赖于一个 DUT 资源。
    /// </summary>
    virtual std::vector<IResource*> dependencies() const { return {}; }

    /// <summary>
    /// 返回指定依赖资源的打开策略（对标 OpenTAP ResourceOpenAttribute）。
    /// - Before：依赖先打开，当前资源再打开（默认）
    /// - InParallel：依赖和当前资源并行打开
    /// - Ignore：不自动打开该依赖
    /// 默认对所有依赖返回 Before。子类可重写以声明不同的策略。
    /// </summary>
    virtual ResourceOpenPolicy dependencyPolicy(IResource* /*dep*/) const {
        return ResourceOpenPolicy::Before;
    }
};

} // namespace eon::sdk
