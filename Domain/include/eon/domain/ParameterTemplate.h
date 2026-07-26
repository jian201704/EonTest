#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QVariant>

#include "eon/core/Entity.h"

namespace eon::domain {

// ============================================================================
// ParamType — 参数类型
// ============================================================================
enum class ParamType {
    String,
    Integer,
    Double,
    Bool,
    Enum,
    HexBytes,     // 十六进制字节串 (CAN payload)
    Json
};

inline QString paramTypeToString(ParamType t) {
    switch (t) {
    case ParamType::String:   return "string";
    case ParamType::Integer:  return "integer";
    case ParamType::Double:   return "double";
    case ParamType::Bool:     return "bool";
    case ParamType::Enum:     return "enum";
    case ParamType::HexBytes: return "hexbytes";
    case ParamType::Json:     return "json";
    }
    return "string";
}

inline ParamType paramTypeFromString(const QString& s) {
    const QString l = s.toLower().trimmed();
    if (l == "integer" || l == "int")  return ParamType::Integer;
    if (l == "double" || l == "float") return ParamType::Double;
    if (l == "bool")    return ParamType::Bool;
    if (l == "enum")    return ParamType::Enum;
    if (l == "hexbytes" || l == "hex") return ParamType::HexBytes;
    if (l == "json")    return ParamType::Json;
    return ParamType::String;
}

// ============================================================================
// ParameterDef — 单个参数定义
// ============================================================================
struct ParameterDef {
    QString name;                // 参数名，如 "canId", "timeoutMs"
    QString displayName;         // 显示名
    QString description;         // 描述
    ParamType type = ParamType::String;
    QVariant defaultValue;       // 默认值
    QVariant minValue;           // 最小值 (Integer/Double)
    QVariant maxValue;           // 最大值 (Integer/Double)
    QStringList enumValues;      // 枚举值列表 (Enum 类型)
    QString unit;                // 单位，如 "ms", "V", "Ω"
    bool required = false;       // 是否必填

    QVariantMap toVariantMap() const {
        return {
            {"name", name}, {"displayName", displayName}, {"description", description},
            {"type", paramTypeToString(type)}, {"defaultValue", defaultValue},
            {"minValue", minValue}, {"maxValue", maxValue},
            {"enumValues", enumValues}, {"unit", unit}, {"required", required}
        };
    }
};

// ============================================================================
// ParameterTemplate — 参数模板（定义一组参数）
// 关联到 workflowId 或 stepId
// ============================================================================
class ParameterTemplate : public eon::core::Entity {
public:
    ParameterTemplate() = default;

    QString name() const { return name_; }
    void setName(const QString& n) { name_ = n; }

    QString workflowId() const { return workflowId_; }
    void setWorkflowId(const QString& id) { workflowId_ = id; }

    QString stepId() const { return stepId_; }
    void setStepId(const QString& id) { stepId_ = id; }

    QString version() const { return version_; }
    void setVersion(const QString& v) { version_ = v; }

    QList<ParameterDef> parameters() const { return parameters_; }
    void setParameters(const QList<ParameterDef>& p) { parameters_ = p; }
    void addParameter(const ParameterDef& p) { parameters_.append(p); }

    QVariantMap toVariantMap() const {
        QVariantMap m;
        m["id"] = id(); m["name"] = name_;
        m["workflowId"] = workflowId_; m["stepId"] = stepId_;
        m["version"] = version_;
        QVariantList plist;
        for (const auto& p : parameters_) plist.append(p.toVariantMap());
        m["parameters"] = plist;
        return m;
    }

private:
    QString name_;
    QString workflowId_;
    QString stepId_;
    QString version_ = "1.0";
    QList<ParameterDef> parameters_;
};

// ============================================================================
// Recipe — 配方（模板的参数值实例）
// 一次测试运行的完整参数集合
// ============================================================================
class Recipe : public eon::core::Entity {
public:
    Recipe() = default;

    QString name() const { return name_; }
    void setName(const QString& n) { name_ = n; }

    QString templateId() const { return templateId_; }
    void setTemplateId(const QString& id) { templateId_ = id; }

    QString workflowId() const { return workflowId_; }
    void setWorkflowId(const QString& id) { workflowId_ = id; }

    QString version() const { return version_; }
    void setVersion(const QString& v) { version_ = v; }

    QString description() const { return description_; }
    void setDescription(const QString& d) { description_ = d; }

    QDateTime createdAt() const { return createdAt_; }
    void setCreatedAt(const QDateTime& dt) { createdAt_ = dt; }

    /// 参数值映射: paramName -> value
    QVariantMap parameterValues() const { return paramValues_; }
    void setParameterValues(const QVariantMap& values) { paramValues_ = values; }
    void setParameter(const QString& name, const QVariant& value) {
        paramValues_[name] = value;
    }
    QVariant parameter(const QString& name, const QVariant& defaultVal = {}) const {
        return paramValues_.value(name, defaultVal);
    }

    /// 注入到 WorkflowContext 的 initialData
    QVariantMap toInitialData() const { return paramValues_; }

    QVariantMap toVariantMap() const {
        return {
            {"id", id()}, {"name", name_}, {"templateId", templateId_},
            {"workflowId", workflowId_}, {"version", version_},
            {"description", description_},
            {"createdAt", createdAt_.toString(Qt::ISODateWithMs)},
            {"parameterValues", paramValues_}
        };
    }

private:
    QString name_;
    QString templateId_;
    QString workflowId_;
    QString version_ = "1.0";
    QString description_;
    QDateTime createdAt_ = QDateTime::currentDateTimeUtc();
    QVariantMap paramValues_;
};

// ============================================================================
// RecipeVersion — 配方版本记录
// ============================================================================
struct RecipeVersion {
    QString recipeId;
    QString version;          // "1.0", "1.1", "2.0"
    QDateTime createdAt;
    QString changeLog;        // 变更说明
    QVariantMap snapshot;     // 当时完整的参数值快照
};

} // namespace eon::domain
