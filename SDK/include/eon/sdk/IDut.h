#pragma once

#include "IResource.h"
#include <QString>
#include <QtPlugin>

namespace eon::sdk {

/// <summary>
/// DUT（Device Under Test，被测设备）接口。
/// 继承 IResource，拥有 Open/Close 生命周期，扩展了设备标识属性。
/// 
/// 对标 OpenTAP 的 IDut : IResource 模型。
/// 每个 CELL 可持有一个或多个 DUT 实例，测试结果与 DUT ID 绑定追溯。
/// </summary>
class IDut : public IResource {
public:
    ~IDut() override = default;

    /// <summary>
    /// DUT 唯一标识（序列号或资产编号）。
    /// </summary>
    virtual QString dutId() const = 0;

    /// <summary>
    /// 产品型号。
    /// </summary>
    virtual QString modelName() const = 0;

    /// <summary>
    /// 固件/软件版本。
    /// </summary>
    virtual QString firmwareVersion() const { return {}; }

    /// <summary>
    /// 附加描述信息。
    /// </summary>
    virtual QString description() const { return {}; }
};

} // namespace eon::sdk

/// <summary>
/// DUT 插件接口 IID（用于 Qt 插件系统）。
/// </summary>
#define EON_IDUT_PLUGIN_IID "com.eontest.sdk.IDutPlugin/1.0"

Q_DECLARE_INTERFACE(eon::sdk::IDut, EON_IDUT_PLUGIN_IID)
