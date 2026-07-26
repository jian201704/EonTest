#pragma once

#include <QString>

namespace eon::domain {
struct WorkflowDefinition;
} // namespace eon::domain

namespace eon::infra {

/// 解析 .xlsx 文件（三 Sheet 格式），直接填充 WorkflowDefinition
///
/// 读取规则：
///   Sheet1 "测试工步表" → workflow.steps[]
///     A:工步号  B:设备分类  C:设备名称  D:通道  E:测试项  F:动作指令
///     G:配置1  H:配置2  I:配置3  J:下限  K:上限  L:单位
///     M:超时ms  N:失败处理  O:备注
///     失败处理映射：停机→fail_fast, 继续→continue_on_error, "重试 N"→maxRetries=N
///     默认按行号顺序执行（step.N → step.N+1）
///
///   Sheet2 "设备资源映射" → workflow.initialData["instruments"]
///   Sheet3 "产品追溯信息" → workflow.initialData["productInfo"]
///
/// 依赖：Windows PowerShell 5.0+ (Expand-Archive)，用于解压 ZIP
bool parseWorkflowDefinitionXlsx(
    const QString& filePath,
    eon::domain::WorkflowDefinition* workflowDefinition,
    QString* errorMessage = nullptr
);

/// 将 WorkflowDefinition 写入 .xlsx 文件（三 Sheet 格式）
/// 返回 true 表示写入成功
bool writeWorkflowDefinitionXlsx(
    const QString& filePath,
    const eon::domain::WorkflowDefinition& workflowDefinition,
    QString* errorMessage = nullptr
);

} // namespace eon::infra
