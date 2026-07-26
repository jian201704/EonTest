#pragma once

#include <QString>
#include <QVariantMap>
#include <QtPlugin>

#include "IPlugin.h"
#include "WorkflowContext.h"
#include "ValidationRule.h"

namespace eon::sdk {

class ResourceManager;
class IDut;
class IArtifactPublisher;

/// <summary>
/// 步骤插件接口。
/// 对标 OpenTAP ITestStep : ITapPlugin。
/// </summary>
class IStepPlugin : public IPlugin {
public:
    ~IStepPlugin() override = default;

    /// <summary>
    /// 执行前回调。对标 OpenTAP PrePlanRun。
    /// 引擎在每个步骤执行 executeStep 前调用。
    /// 可用于资源准备、参数校验等。
    /// </summary>
    virtual void preExecute(WorkflowContext& context) { Q_UNUSED(context); }

    /// <summary>
    /// 执行步骤逻辑。
    /// </summary>
    virtual bool executeStep(WorkflowContext& context, QString& errorMessage) = 0;

    /// <summary>
    /// 执行后回调。对标 OpenTAP PostPlanRun。
    /// 引擎在每个步骤 executeStep 完成后调用（无论成败）。
    /// 可用于资源释放、结果汇总等。
    /// </summary>
    virtual void postExecute(WorkflowContext& context) { Q_UNUSED(context); }

    /// 工作流结束回调。用于释放跨步骤持有的会话级资源。
    virtual void postWorkflow(WorkflowContext& context) { Q_UNUSED(context); }

    /// <summary>
    /// 返回声明式验证规则列表（对标 OpenTAP ValidatingObject.Rules）。
    /// 在插件构造时注册，Studio 在属性面板实时检查。
    /// 默认返回空。
    /// </summary>
    virtual std::vector<ValidationRule> validationRules() const { return {}; }

    /// <summary>
    /// 设置 Artifact 发布器引用（引擎注入）。
    /// 步骤通过此接口发布文件和流 artifact。
    /// </summary>
    virtual void setArtifactPublisher(IArtifactPublisher* /*publisher*/) {}
};

/// <summary>
/// 分析器插件接口。
/// 对标 OpenTAP IResultListener。
/// </summary>
class IAnalyzerPlugin : public IPlugin {
public:
    ~IAnalyzerPlugin() override = default;

    virtual QString id() const override = 0;
    virtual bool analyze(const WorkflowContext& context, QVariantMap& result, QString& errorMessage) = 0;
};

/// <summary>
/// 报告器插件接口。
/// </summary>
class IReporterPlugin : public IPlugin {
public:
    ~IReporterPlugin() override = default;

    virtual QString id() const override = 0;
    virtual bool report(const WorkflowContext& context, QString& errorMessage) = 0;
};

} // namespace eon::sdk

#define EON_ISTEPPLUGIN_IID "com.eontest.sdk.IStepPlugin/1.0"
#define EON_IANALYZERPLUGIN_IID "com.eontest.sdk.IAnalyzerPlugin/1.0"
#define EON_IREPORTERPLUGIN_IID "com.eontest.sdk.IReporterPlugin/1.0"

Q_DECLARE_INTERFACE(eon::sdk::IStepPlugin, EON_ISTEPPLUGIN_IID)
Q_DECLARE_INTERFACE(eon::sdk::IAnalyzerPlugin, EON_IANALYZERPLUGIN_IID)
Q_DECLARE_INTERFACE(eon::sdk::IReporterPlugin, EON_IREPORTERPLUGIN_IID)
