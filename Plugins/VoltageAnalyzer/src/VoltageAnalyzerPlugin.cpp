#include <QObject>
#include <QVariantMap>

#include "eon/sdk/IStepPlugin.h"

// ============================================================================
// VoltageAnalyzerPlugin — 电压判定分析器
//
// context.data 参数：
//   voltageMin        - 电压下限 V（默认 3.0）
//   voltageMax        - 电压上限 V（默认 3.6）
//   measuredValue     - 实测值（由 Multimeter 写入）
//   measuredUnit      - 单位（由 Multimeter 写入）
//
// 输出到 result：
//   analyze.passed    - true/false
//   analyze.value     - 实测值
//   analyze.min       - 下限
//   analyze.max       - 上限
//   analyze.unit      - 单位
//   analyze.message   - 判定描述
// ============================================================================
class VoltageAnalyzerPlugin final : public QObject
    , public eon::sdk::IStepPlugin
    , public eon::sdk::IAnalyzerPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "voltageanalyzer.json")
    Q_INTERFACES(eon::sdk::IStepPlugin eon::sdk::IAnalyzerPlugin)

public:
    QString id() const override { return "voltage.analyzer"; }

    // 作为步骤调用：从 context.data 读取 measuredValue，执行判定
    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        QVariantMap result;
        if (!analyze(context, result, errorMessage)) {
            return false;
        }
        // 把判定结果写回 context.data，供后续步骤/analyzer 使用
        for (auto it = result.cbegin(); it != result.cend(); ++it)
            context.data.insert(it.key(), it.value());
        return true;
    }

    bool analyze(const eon::sdk::WorkflowContext& context,
                 QVariantMap& result, QString& errorMessage) override
    {
        const double voltageMin = context.data.value("voltageMin", 3.0).toDouble();
        const double voltageMax = context.data.value("voltageMax", 3.6).toDouble();
        // 优先读新命名空间 power.voltage，回退到旧名 measuredValue
        double measuredValue = context.data.value("power.voltage",
            context.data.value("measuredValue")).toDouble();
        const QString measuredUnit = context.data.value("measuredUnit", "V").toString();

        if (voltageMin >= voltageMax) {
            errorMessage = "VoltageAnalyzer: voltageMin >= voltageMax is invalid.";
            return false;
        }

        const bool passed = (measuredValue >= voltageMin && measuredValue <= voltageMax);

        result.insert("analyze.passed", passed);
        result.insert("analyze.value", measuredValue);
        result.insert("analyze.min", voltageMin);
        result.insert("analyze.max", voltageMax);
        result.insert("analyze.unit", measuredUnit);
        result.insert("analyze.workflowId", context.workflowId);

        if (passed) {
            result.insert("analyze.message",
                QString("PASS: %1 %2 within [%3, %4] %2")
                    .arg(measuredValue, 0, 'f', 3)
                    .arg(measuredUnit)
                    .arg(voltageMin, 0, 'f', 3)
                    .arg(voltageMax, 0, 'f', 3));
        } else {
            result.insert("analyze.message",
                QString("FAIL: %1 %2 outside [%3, %4] %2")
                    .arg(measuredValue, 0, 'f', 3)
                    .arg(measuredUnit)
                    .arg(voltageMin, 0, 'f', 3)
                    .arg(voltageMax, 0, 'f', 3));
            errorMessage = result["analyze.message"].toString();
            return false;
        }

        return true;
    }
};

#include "VoltageAnalyzerPlugin.moc"
