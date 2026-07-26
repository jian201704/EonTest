#pragma once

#include <string>
#include <vector>

#include <QString>
#include <QVariantMap>

#include "eon/sdk/Verdict.h"

namespace eon::sdk {

/// <summary>
/// Analyzer 分析结果（参考 OpenTAP 结果映射 + 章节 13 设计）。
/// 每个 Analyzer 插件产出此结果，引擎按策略合并为 Verdict。
/// </summary>
struct AnalyzerResult {
    std::string analyzerId;     ///< 分析器标识
    bool hasValue = false;      ///< 是否包含测量值
    double value = 0.0;         ///< 测量值
    std::string status;         ///< "pass"/"warn"/"fail"/"error"/"no_result"
    std::string message;        ///< 描述信息
    double min = 0.0;           ///< 下限（可选）
    double max = 0.0;           ///< 上限（可选）
    std::string unit;           ///< 单位（可选）

    /// 转为 QVariantMap（用于存入 context.data / 事件总线）
    QVariantMap toVariantMap() const;
};

/// <summary>
/// 合并单个 AnalyzerResult 为 Verdict
/// </summary>
inline Verdict mergeSingleResult(const AnalyzerResult& res) {
    if (res.status == "error")     return Verdict::Error;
    if (res.status == "fail")      return Verdict::Fail;
    if (res.status == "warn")      return Verdict::Inconclusive;
    if (res.status == "no_result") return Verdict::Inconclusive;
    return Verdict::Pass;
}

/// AnalyzerResult::toVariantMap 实现（在 mergeSingleResult 之后）
inline QVariantMap AnalyzerResult::toVariantMap() const {
    return {
        {"analyzerId", QString::fromStdString(analyzerId)},
        {"hasValue", hasValue},
        {"value", value},
        {"status", QString::fromStdString(status)},
        {"message", QString::fromStdString(message)},
        {"min", min},
        {"max", max},
        {"unit", QString::fromStdString(unit)},
        {"verdict", verdictToString(mergeSingleResult(*this))}
    };
}

/// <summary>
/// 合并多个 AnalyzerResult 为最终 Verdict（优先级：Error > Aborted > Fail > Inconclusive > Pass）
/// 对应章节 13 的合并规则。
/// </summary>
inline Verdict mergeAnalyzerResults(const std::vector<AnalyzerResult>& results) {
    Verdict merged = Verdict::NotSet;
    for (const auto& res : results) {
        merged = mergeVerdicts(merged, mergeSingleResult(res));
    }
    return merged == Verdict::NotSet ? Verdict::Pass : merged;
}

/// <summary>
/// 从 WorkflowContext.data 提取 AnalyzerResult（由 VoltageAnalyzerPlugin 等写入）
/// </summary>
inline AnalyzerResult analyzerResultFromContextData(const QVariantMap& data) {
    AnalyzerResult res;
    if (data.contains("analyze.passed"))
        res.status = data.value("analyze.passed").toBool() ? "pass" : "fail";
    if (data.contains("analyze.value"))
        res.hasValue = true; res.value = data.value("analyze.value").toDouble();
    if (data.contains("analyze.min"))
        res.min = data.value("analyze.min").toDouble();
    if (data.contains("analyze.max"))
        res.max = data.value("analyze.max").toDouble();
    if (data.contains("analyze.unit"))
        res.unit = data.value("analyze.unit").toString().toStdString();
    if (data.contains("analyze.message"))
        res.message = data.value("analyze.message").toString().toStdString();
    return res;
}

} // namespace eon::sdk
