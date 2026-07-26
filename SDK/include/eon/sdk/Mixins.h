#pragma once

#include <QString>
#include <QVector>
#include <functional>

namespace eon::sdk {

/// <summary>
/// Mixin 构建器接口（对标 OpenTAP IMixinBuilder）。
/// 运行时动态为步骤/资源添加属性和行为。
///
/// Mixin 类型：
/// - ITestStepPreRunMixin  → 步骤 Run() 前执行
/// - ITestStepPostRunMixin → 步骤 Run() 后执行
/// - ITestPlanPreRunMixin  → Plan 执行前执行一次
/// </summary>

/// <summary>
/// 步骤执行前 Mixin（对标 OpenTAP ITestStepPreRunMixin）。
/// </summary>
class IStepPreRunMixin {
public:
    virtual ~IStepPreRunMixin() = default;
    virtual void onPreRun(struct WorkflowContext& context) = 0;
};

/// <summary>
/// 步骤执行后 Mixin（对标 OpenTAP ITestStepPostRunMixin）。
/// </summary>
class IStepPostRunMixin {
public:
    virtual ~IStepPostRunMixin() = default;
    virtual void onPostRun(const struct WorkflowContext& context,
                           const struct StepRun& stepRun) = 0;
};

/// <summary>
/// Plan 执行前 Mixin（对标 OpenTAP ITestPlanPreRunMixin）。
/// </summary>
class IPlanPreRunMixin {
public:
    virtual ~IPlanPreRunMixin() = default;
    virtual void onPlanPreRun(struct WorkflowContext& context) = 0;
};

/// <summary>
/// Mixin 构建器工厂接口（对标 OpenTAP IMixinBuilder）。
/// 每个 Mixin 类型实现一次，负责创建 Mixin 实例。
/// </summary>
class IMixinBuilder {
public:
    virtual ~IMixinBuilder() = default;

    /// Mixin 显示名称
    virtual QString displayName() const = 0;

    /// Mixin 描述
    virtual QString description() const = 0;

    /// 创建 Mixin 实例（内含所有 PreRun/PostRun/PlanPreRun 实现）
    virtual QVector<QObject*> createMixins() = 0;

    /// 此 Mixin 可应用于哪些目标类型
    virtual QStringList supportedTargetTypes() const = 0;
};

/// <summary>
/// Mixin 管理器。
/// 维护所有已注册的 MixinBuilder，供 Studio "Add Mixin" 菜单使用。
/// </summary>
class MixinManager {
public:
    static MixinManager& instance();

    /// 注册 Mixin 构建器
    void registerBuilder(IMixinBuilder* builder);

    /// 获取所有已注册的构建器
    const QVector<IMixinBuilder*>& builders() const { return builders_; }

private:
    MixinManager() = default;
    QVector<IMixinBuilder*> builders_;
};

} // namespace eon::sdk
