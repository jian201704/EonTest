#pragma once

#include <QList>
#include <QVariantMap>
#include <cmath>
#include <algorithm>

namespace eon::domain {

// ============================================================================
// SpcCalculator — SPC 统计过程控制
// 计算 CPK/PPK/X̄-R 控制图等
// ============================================================================
class SpcCalculator {
public:
    /// 单值列表 → SPC 统计
    struct SpcResult {
        int count = 0;
        double mean = 0.0;        // X̄ 均值
        double stdDev = 0.0;      // σ 标准差
        double min = 0.0;         // 最小值
        double max = 0.0;         // 最大值
        double cp = 0.0;          // 过程能力指数 Cp
        double cpk = 0.0;         // 过程能力指数 Cpk
        double pp = 0.0;          // 过程性能指数 Pp
        double ppk = 0.0;         // 过程性能指数 Ppk
        double usl = 0.0;         // 上规格限
        double lsl = 0.0;         // 下规格限
        double passRate = 0.0;    // 合格率 %
    };

    /// 计算 SPC 统计
    /// @param values 测量值列表
    /// @param usl 上规格限 (Upper Spec Limit)
    /// @param lsl 下规格限 (Lower Spec Limit)
    static SpcResult calculate(const QList<double>& values, double usl, double lsl) {
        SpcResult r;
        r.count = values.size();
        if (r.count == 0) return r;

        r.usl = usl;
        r.lsl = lsl;

        // 均值
        double sum = 0.0;
        for (double v : values) sum += v;
        r.mean = sum / r.count;

        // 最小/最大
        r.min = *std::min_element(values.begin(), values.end());
        r.max = *std::max_element(values.begin(), values.end());

        // 标准差
        double sqSum = 0.0;
        for (double v : values) sqSum += (v - r.mean) * (v - r.mean);
        r.stdDev = std::sqrt(sqSum / r.count);

        // Cp / Cpk / Pp / Ppk
        const double tolerance = usl - lsl;
        if (tolerance > 0 && r.stdDev > 0) {
            r.cp = tolerance / (6.0 * r.stdDev);
            const double cpu = (usl - r.mean) / (3.0 * r.stdDev);
            const double cpl = (r.mean - lsl) / (3.0 * r.stdDev);
            r.cpk = std::min(cpu, cpl);
            r.pp = r.cp;  // 简化：Pp ≈ Cp（假设过程稳定
            r.ppk = r.cpk;
        }

        // 合格率
        int inSpec = 0;
        for (double v : values) {
            if (v >= lsl && v <= usl) inSpec++;
        }
        r.passRate = static_cast<double>(inSpec) / r.count * 100.0;

        return r;
    }

    /// 从 QVariantMap 批量提取同名测量值
    static QList<double> extractValues(const QList<QVariantMap>& measurements,
                                        const QString& name) {
        QList<double> values;
        for (const auto& m : measurements) {
            if (m.value("name").toString() == name) {
                values.append(m.value("value").toDouble());
            }
        }
        return values;
    }

    /// SPC 结果 → 报告友好的 Map
    static QVariantMap toReport(const SpcResult& r) {
        return {
            {"count", r.count}, {"mean", r.mean}, {"stdDev", r.stdDev},
            {"min", r.min}, {"max", r.max},
            {"cp", r.cp}, {"cpk", r.cpk}, {"pp", r.pp}, {"ppk", r.ppk},
            {"usl", r.usl}, {"lsl", r.lsl}, {"passRate", r.passRate},
            {"judgment", r.cpk >= 1.33 ? "PASS (Cpk≥1.33)"
                        : r.cpk >= 1.0  ? "MARGINAL (1.0≤Cpk<1.33)"
                        : "FAIL (Cpk<1.0)"}
        };
    }
};

} // namespace eon::domain
