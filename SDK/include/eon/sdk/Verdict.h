#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace eon::sdk {

/// Verdict 判定等级（与 OpenTAP 对齐）
///
/// 优先级（从高到低）：
///   Error > Aborted > Fail > Inconclusive > Pass
///
enum class Verdict : int {
    NotSet       = 0,   ///< 未设置（初始状态）
    Pass         = 10,  ///< 通过
    Inconclusive = 20,  ///< 不确定（通过但带警告，或测量值在边界）
    Fail         = 30,  ///< 失败（测量值超限）
    Aborted      = 40,  ///< 中断（用户取消或前置步骤失败导致跳过）
    Error        = 50   ///< 错误（异常、通信失败、插件崩溃）
};

/// 将 Verdict 转为可读字符串
inline QString verdictToString(Verdict v) {
    switch (v) {
    case Verdict::NotSet:       return QStringLiteral("NotSet");
    case Verdict::Pass:         return QStringLiteral("Pass");
    case Verdict::Inconclusive: return QStringLiteral("Inconclusive");
    case Verdict::Fail:         return QStringLiteral("Fail");
    case Verdict::Aborted:      return QStringLiteral("Aborted");
    case Verdict::Error:        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

/// 合并多个 Verdict（按优先级取最高者）
inline Verdict mergeVerdicts(const Verdict& a, const Verdict& b) {
    // 使用 int 值比较，数值越大优先级越高
    return static_cast<int>(a) >= static_cast<int>(b) ? a : b;
}

/// 从字符串解析 Verdict
inline Verdict verdictFromString(const QString& str) {
    if (str == QStringLiteral("Pass"))         return Verdict::Pass;
    if (str == QStringLiteral("Inconclusive")) return Verdict::Inconclusive;
    if (str == QStringLiteral("Fail"))         return Verdict::Fail;
    if (str == QStringLiteral("Aborted"))      return Verdict::Aborted;
    if (str == QStringLiteral("Error"))        return Verdict::Error;
    return Verdict::NotSet;
}

/// <summary>
/// UpgradeVerdict — 对齐 OpenTAP TestStepRun.UpgradeVerdict 模式。
/// Verdict 枚举值越大优先级越高：NotSet(0) < Pass(10) < Inconclusive(20) < Fail(30) < Aborted(40) < Error(50)
/// </summary>
inline void upgradeVerdict(Verdict& current, Verdict newVerdict) {
    if (static_cast<int>(current) < static_cast<int>(newVerdict)) {
        current = newVerdict;
    }
}

} // namespace eon::sdk
