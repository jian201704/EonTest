#pragma once

#include <QString>
#include <stdexcept>

namespace eon::sdk {

/// <summary>
/// 类型安全的步骤间输入参数（对标 OpenTAP Input<T>）。
/// 从一个步骤的 Output<T> 接收值。
/// 如果未连接则访问 Value 时抛异常。
/// </summary>
template <typename T>
class Input {
public:
    Input() = default;

    /// 是否已连接到输出
    bool isConnected() const { return connected_; }

    /// 获取输入值（未连接时抛异常）
    const T& value() const {
        if (!connected_)
            throw std::runtime_error("Input is not connected to an output.");
        return value_;
    }

    /// 内部使用：由引擎设置值
    void setValue(const T& v) { value_ = v; connected_ = true; }

private:
    T value_{};
    bool connected_ = false;
};

/// <summary>
/// 类型安全的步骤间输出参数（对标 OpenTAP [Output] 属性）。
/// 步骤执行后设置值，供下游步骤通过 Input<T> 读取。
/// </summary>
template <typename T>
class Output {
public:
    Output() = default;

    /// 获取输出值
    const T& value() const { return value_; }

    /// 设置输出值
    void setValue(const T& v) { value_ = v; hasValue_ = true; }

    /// 是否已产出值
    bool hasValue() const { return hasValue_; }

private:
    T value_{};
    bool hasValue_ = false;
};

} // namespace eon::sdk
