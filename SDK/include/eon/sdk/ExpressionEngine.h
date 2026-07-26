#pragma once

#include <QString>
#include <QVariant>
#include <functional>

namespace eon::sdk {

/// <summary>
/// 表达式引擎（对标 OpenTAP Expressions 插件）。
/// 支持：算术、三角函数、字符串插值、步骤输出引用、自定义函数。
///
/// 使用示例：
///   ExpressionEngine::evaluate("10 * 60") → 600.0
///   ExpressionEngine::evaluate("@step1.power * 0.001", context) → 3.14
///   ExpressionEngine::evaluateString("BANDwidth {2000 * 1000000 / 10}") → "BANDwidth 200000000"
/// </summary>
class ExpressionEngine {
public:
    /// 获取单例
    static ExpressionEngine& instance();

    /// 注册自定义函数
    using CustomFunc = std::function<double(const QVector<double>&)>;
    void registerFunction(const QString& name, CustomFunc func);

    /// 注册自定义常量
    void registerConstant(const QString& name, double value);

    /// 求值数值表达式
    /// @param expr 表达式字符串（如 "10 * 60", "sin(pi/2)"）
    /// @param contextValues 可选：步骤输出引用键值对（key=步骤名.属性名）
    /// @param error 输出：错误信息
    /// @return 计算结果，失败时返回 NaN
    double evaluate(const QString& expr,
                    const QVariantMap& contextValues = {},
                    QString* error = nullptr);

    /// 求值字符串模板（支持 {expr} 插值）
    /// @param template_ 模板字符串（如 "FREQ {1e6 * 2} Hz"）
    /// @param contextValues 可选：步骤输出引用键值对
    /// @param error 输出：错误信息
    /// @return 插值后的字符串
    QString evaluateString(const QString& template_,
                           const QVariantMap& contextValues = {},
                           QString* error = nullptr);

private:
    ExpressionEngine();
    ~ExpressionEngine();

    struct Impl;
    Impl* impl_;
};

} // namespace eon::sdk
