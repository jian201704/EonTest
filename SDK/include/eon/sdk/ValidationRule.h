#pragma once

#include <QString>
#include <functional>

namespace eon::sdk {

/// <summary>
/// 声明式验证规则（对标 OpenTAP ValidatingObject.Rules）。
/// 在插件构造时注册，Studio 在属性面板实时检查并显示警告。
/// </summary>
class ValidationRule {
public:
    using CheckFunc = std::function<bool()>;

    /// <param name="check">验证函数，返回 true=通过</param>
    /// <param name="message">失败时显示的消息</param>
    /// <param name="affectedProperties">受影响的属性名列表（用于 UI 高亮）</param>
    ValidationRule(CheckFunc check, QString message, QStringList affectedProperties)
        : check_(std::move(check))
        , message_(std::move(message))
        , affectedProperties_(std::move(affectedProperties)) {}

    /// 执行验证，返回 true=通过
    bool validate(QString* errorMessage = nullptr) const {
        bool ok = check_();
        if (!ok && errorMessage) *errorMessage = message_;
        return ok;
    }

    QString message() const { return message_; }
    QStringList affectedProperties() const { return affectedProperties_; }

private:
    CheckFunc check_;
    QString message_;
    QStringList affectedProperties_;
};

} // namespace eon::sdk
