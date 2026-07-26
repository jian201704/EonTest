#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <QHash>
#include <QString>

#include "eon/core/EventBus.h"
#include "eon/domain/WorkflowDefinition.h"
#include "eon/sdk/IStepPlugin.h"
#include "eon/sdk/ResourceManager.h"
#include "eon/sdk/Verdict.h"
#include "eon/sdk/TraceEvent.h"
#include "eon/sdk/AnalyzerResult.h"
#include "eon/sdk/StepResult.h"
#include "eon/sdk/ArtifactPipeline.h"
#include "eon/sdk/CapabilityRegistry.h"
#include "eon/sdk/MatrixManager.h"
#include "eon/sdk/RetryPolicy.h"
#include "eon/sdk/IDut.h"
#include "eon/runtime/PluginManager.h"
#include "eon/runtime/ResourceDependencyAnalyzer.h"
#include "eon/runtime/EnvSnapshot.h"
#include "eon/runtime/CellWorker.h"

namespace eon::runtime {

/// 执行控制指令（由 onBreakOffered 回调返回）
enum class ExecutionControl {
    Continue,   // 继续执行
    Pause,      // 暂停（回调会阻塞直到返回 Continue）
    Skip,       // 跳过当前步骤
    Abort       // 中止整个工作流
};

class WorkflowEngine {
public:
    explicit WorkflowEngine(eon::core::EventBus* eventBus);
    ~WorkflowEngine();

    bool loadPlugins(const QString& pluginDirectory, QString* errorMessage = nullptr);
    bool executeWorkflow(const eon::domain::WorkflowDefinition& workflow, QString* errorMessage = nullptr);

    /// 执行工作流并注入配方参数（合并到 initialData，配方值优先）
    bool executeWorkflowWithParams(const eon::domain::WorkflowDefinition& workflow,
                                   const QVariantMap& recipeParams,
                                   QString* errorMessage = nullptr);
    bool executeMinimalWorkflow(QString* errorMessage = nullptr);

    /// 设置资源管理器（引擎不拥有所有权）
    void setResourceManager(eon::sdk::ResourceManager* rm) { resourceManager_ = rm; }

    /// 获取资源管理器
    eon::sdk::ResourceManager* resourceManager() const { return resourceManager_; }

    /// 获取最终 Verdict
    eon::sdk::Verdict finalVerdict() const { return finalVerdict_; }

    /// 设置报告输出目录（用于 scpi.trace JSON Lines / env_snapshot）
    void setReportsDir(const QString& dir) { reportsDir_ = dir; }

    /// 设置 CELL ID（用于多 CELL 日志隔离）
    void setCellId(const QString& cellId) { cellId_ = cellId; }

    /// 获取当前 CELL ID
    QString cellId() const { return cellId_; }

    /// ============================================================
    /// CellWorker 多 CELL 支持（章节 6）
    /// ============================================================

    /// 创建并启动一个 CellWorker
    CellWorker* createCellWorker(const QString& cellId);

    /// 获取所有 CellWorker
    std::vector<CellWorker*> cellWorkers() const;

    /// 停止并销毁所有 CellWorker
    void destroyAllCellWorkers();

    /// 检查所有 CellWorker 健康状态，返回不健康的列表
    QStringList unhealthyCellWorkers() const;

    /// 设置重试策略（全局默认）
    void setDefaultRetryPolicy(const eon::sdk::RetryPolicy& policy) { defaultRetryPolicy_ = policy; }

    /// ============================================================
    /// 外部参数（对标 OpenTAP ExternalParameters）
    /// ============================================================

    /// 设置外部参数（CLI --external "Freq=10MHz" 对应）
    void setExternalParameter(const QString& key, const QVariant& value);

    /// 获取所有外部参数
    QVariantMap externalParameters() const { return externalParams_; }

    /// ============================================================
    /// Artifact 管道（对标 OpenTAP IArtifactListener）
    /// ============================================================

    /// 获取 artifact 发布器（步骤通过此接口发布 artifact）
    eon::sdk::ArtifactPipeline* artifactPipeline() { return &artifactPipeline_; }

    /// 注册 artifact 监听器
    void addArtifactListener(eon::sdk::IArtifactListener* listener) {
        artifactPipeline_.addListener(listener);
    }
    /// ============================================================
    /// 暂停/跳过控制（对标 OpenTAP BreakOffered）
    /// ============================================================

    /// 每步执行前回调。返回 ExecutionControl 控制执行流程。
    /// 返回 Pause 时回调会阻塞直到外部调用 resumeExecution()或返回其他指令。
    std::function<ExecutionControl(const QString& stepId)> onBreakOffered;

    /// 恢复暂停的执行（供外部线程调用）
    void resumeExecution() { resumeSignal_.store(true); }
    void skipStep() { skipSignal_.store(true); }
    /// 获取插件管理器
    eon::runtime::PluginManager* pluginManager() { return &pluginManager_; }

    /// 获取能力注册表（P1）
    eon::sdk::CapabilityRegistry* capabilityRegistry() { return &capabilityRegistry_; }

    /// 获取矩阵管理器（P1）
    eon::sdk::MatrixManager* matrixManager() { return &matrixManager_; }
private:
    eon::sdk::IStepPlugin* findStepPluginById(const QString& pluginId) const;

    /// 在执行工作流前预分配所有 Plan 级资源
    bool preallocateResources(const eon::domain::WorkflowDefinition& workflow, QString* errorMessage);

    /// 在工作流执行完毕后释放所有资源
    void releaseAllResources();

    /// 更新最终 Verdict（按优先级合并）
    void updateFinalVerdict(eon::sdk::Verdict stepVerdict);

    /// 写入结构化 SCPI 跟踪事件（JSON Lines）
    void writeScpiTrace(const QString& stepId, const QString& resourceId,
                         const QString& dir, const QString& payload,
                         qint64 durationMs, const QString& status,
                         const QString& error = {},
                         const eon::sdk::WorkflowContext* context = nullptr);

    /// 写入结构化步骤结果
    void writeStepResult(const eon::sdk::StepResult& result);

    /// 从 context.data 收集 Analyzer 结果
    void collectAnalyzerResults(const eon::sdk::WorkflowContext& context);

    /// 捕获环境快照
    void captureSnapshot(const QString& workflowId);

    eon::core::EventBus* eventBus_ = nullptr;
    eon::sdk::ResourceManager* resourceManager_ = nullptr;
    eon::sdk::Verdict finalVerdict_ = eon::sdk::Verdict::NotSet;

    /// 插件管理器
    PluginManager pluginManager_;

    /// 当前工作流持有的所有租约（PrePlanRun 阶段获取）
    std::vector<std::unique_ptr<eon::sdk::Lease>> activeLeases_;

    // CellWorker 列表
    std::vector<std::unique_ptr<CellWorker>> cellWorkers_;
    mutable std::mutex cellWorkerMutex_;

    // 报告/日志路径
    QString reportsDir_;
    QString cellId_ = "cell-default";

    // P1 模块
    eon::sdk::CapabilityRegistry capabilityRegistry_;
    eon::sdk::MatrixManager matrixManager_;

    // 默认重试策略
    eon::sdk::RetryPolicy defaultRetryPolicy_;

    // 外部参数
    QVariantMap externalParams_;

    // Artifact 管道
    eon::sdk::ArtifactPipeline artifactPipeline_;

    // 步骤结果收集（用于 Analyzer→Verdict 映射）
    std::vector<eon::sdk::AnalyzerResult> pendingAnalyzerResults_;

    // 暂停/跳过控制信号
    std::atomic<bool> resumeSignal_{false};
    std::atomic<bool> skipSignal_{false};
    std::atomic<bool> abortSignal_{false};
};

} // namespace eon::runtime
