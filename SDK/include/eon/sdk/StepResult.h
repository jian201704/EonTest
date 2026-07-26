#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariant>
#include <QVariantMap>
#include <QPair>

namespace eon::sdk {

/// <summary>
/// 单列结果数据（对标 OpenTAP ResultColumn）。
/// 一列 = 列名 + N 行同类型数据 + 可选单位。
/// </summary>
struct ResultColumn {
    QString name;              // 列名，如 "Frequency [Hz]"
    QVector<double> values;    // 列数据
    QString unit;              // 单位（可选）

    int rowCount() const { return values.size(); }

    QVariantMap toVariantMap() const {
        QVariantList vals;
        vals.reserve(values.size());
        for (double v : values) vals.append(v);
        return {
            {"name", name},
            {"values", vals},
            {"unit", unit}
        };
    }
};

/// <summary>
/// 结构化结果表（对标 OpenTAP ResultTable）。
/// 一张表 = 表名 + N 列，每列 M 行。
/// </summary>
struct ResultTable {
    QString name;                        // 表名，如 "Sweep Results"
    QVector<ResultColumn> columns;       // N 列

    int rowCount() const {
        return columns.isEmpty() ? 0 : columns[0].rowCount();
    }

    /// 便捷构造：单行多列结果
    static ResultTable singleRow(
        const QString& tableName,
        const QVector<QPair<QString, double>>& nameValuePairs)
    {
        ResultTable t;
        t.name = tableName;
        for (const auto& [colName, val] : nameValuePairs) {
            ResultColumn col;
            col.name = colName;
            col.values.append(val);
            t.columns.append(std::move(col));
        }
        return t;
    }

    /// 便捷构造：多列多行结果（对标 OpenTAP PublishTable）
    static ResultTable fromColumns(
        const QString& tableName,
        std::initializer_list<ResultColumn> cols)
    {
        ResultTable t;
        t.name = tableName;
        t.columns = cols;
        return t;
    }

    QVariantMap toVariantMap() const {
        QVariantList colList;
        colList.reserve(columns.size());
        for (const auto& col : columns)
            colList.append(col.toVariantMap());
        return {
            {"name", name},
            {"columns", colList},
            {"rowCount", rowCount()}
        };
    }

    static ResultTable fromVariantMap(const QVariantMap& map) {
        ResultTable t;
        t.name = map.value("name").toString();
        for (const auto& colVar : map.value("columns").toList()) {
            QVariantMap cm = colVar.toMap();
            ResultColumn col;
            col.name = cm.value("name").toString();
            col.unit = cm.value("unit").toString();
            for (const auto& v : cm.value("values").toList())
                col.values.append(v.toDouble());
            t.columns.append(std::move(col));
        }
        return t;
    }
};

/// <summary>
/// 单步执行产生的结果（对标 OpenTAP TestStepRun + ResultTable 组合）。
/// 每个步骤可以产出 0-N 张 ResultTable + 0-N 个 Artifact 路径。
/// </summary>
struct StepResult {
    QString stepId;
    QString pluginId;
    QString dutId;                         // 关联的 DUT ID
    QString cellId;
    QVector<ResultTable> tables;           // 0-N 张结果表
    QStringList artifactPaths;             // 关联的 artifact 文件路径
    QVariantMap metadata;                  // 自由元数据

    QVariantMap toVariantMap() const {
        QVariantList tableList;
        tableList.reserve(tables.size());
        for (const auto& t : tables)
            tableList.append(t.toVariantMap());

        QVariantMap m;
        m["stepId"] = stepId;
        m["pluginId"] = pluginId;
        m["dutId"] = dutId;
        m["cellId"] = cellId;
        m["tables"] = tableList;
        m["artifactPaths"] = QVariant::fromValue(artifactPaths);
        m["metadata"] = metadata;
        return m;
    }
};

} // namespace eon::sdk
