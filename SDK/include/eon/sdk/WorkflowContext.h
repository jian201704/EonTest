#pragma once

#include <QString>
#include <QVariantMap>

namespace eon::sdk {

class ResourceManager;
class IDut;

/// <summary>
/// 工作流执行上下文。
/// 对标 OpenTAP TestPlanRun。
/// 引擎在每次 executeWorkflow 时创建，注入到每个步骤插件。
/// 步骤可通过此上下文访问资源管理器、DUT、步骤间数据。
/// </summary>
struct WorkflowContext {
    QString workflowId;
    QVariantMap data;

    /// 资源管理器引用（引擎注入，插件用其获取资源租约）
    ResourceManager* resourceManager = nullptr;

    /// DUT 引用（引擎注入，当前步骤关联的被测设备）
    IDut* dut = nullptr;
};

} // namespace eon::sdk
