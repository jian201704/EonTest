#include <QObject>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonDocument>

#include "eon/sdk/IStepPlugin.h"
#include "eon/infra/PythonProcessBackend.h"

class PythonScriptPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "pythonstep.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override { return "python.script"; }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        auto& d = context.data;
        const bool virt = d.value("virtualMode", false).toBool();

        // 从 context 读取 Python 配置
        const QString scriptPath = d.value("script", d.value("scriptPath", "")).toString();
        const QString pythonCmd = d.value("pythonCmd", d.value("command", "python3")).toString();
        const int timeoutMs = d.value("timeoutMs", d.value("timeout", 30000)).toInt();

        if (virt) {
            // 虚拟模式：跳过 Python 调用
            d.insert("python.virtual", true);
            d.insert("python.script", scriptPath);
            d.insert("measuredValue", 0);
            d.insert("measuredUnit", "");
            return true;
        }

        if (scriptPath.isEmpty()) {
            errorMessage = "Python script path not specified. Set 'script' in step config.";
            return false;
        }

        // 使用 PythonProcessBackend 启动 Python 子进程
        eon::infra::PythonProcessBackend py;
        QVariantMap pyConfig;
        pyConfig["command"] = pythonCmd;
        pyConfig["script"] = scriptPath;
        pyConfig["timeoutMs"] = timeoutMs;
        pyConfig["workDir"] = d.value("workDir", "");

        if (!py.open(pyConfig, errorMessage)) {
            return false;
        }

        // 构建 JSON 请求
        QJsonObject request;
        request["cmd"] = d.value("action", d.value("cmd", "execute")).toString();
        request["workflowId"] = d.value("workflowId", "").toString();
        request["stepId"] = d.value("_currentStepId", "").toString();

        // 传递所有 context 参数给 Python 脚本
        QJsonObject params;
        for (auto it = d.constBegin(); it != d.constEnd(); ++it)
            params[it.key()] = QJsonValue::fromVariant(it.value());
        request["params"] = params;

        // 发送请求，等待响应
        QJsonObject response = py.sendRequest(request, timeoutMs, errorMessage);

        // 关闭 Python 进程
        py.close();

        if (response.isEmpty()) {
            errorMessage = "Python script returned empty response.";
            return false;
        }

        // 检查错误
        if (response.contains("error") && !response["error"].toString().isEmpty()) {
            errorMessage = QString("Python error: %1").arg(response["error"].toString());
            return false;
        }

        // 将 Python 返回的结果写回 context
        QJsonObject result = response["result"].toObject();
        for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
            d[it.key()] = it.value().toVariant();
        }

        d.insert("python.executed", true);
        d.insert("python.script", scriptPath);

        // 如果 Python 返回了测量值，透传给框架
        if (result.contains("measuredValue"))
            d["measuredValue"] = result["measuredValue"].toVariant();
        if (result.contains("measuredUnit"))
            d["measuredUnit"] = result["measuredUnit"].toString();
        if (result.contains("lowerLimit"))
            d["lowerLimit"] = result["lowerLimit"].toVariant();
        if (result.contains("upperLimit"))
            d["upperLimit"] = result["upperLimit"].toVariant();
        if (result.contains("resultText"))
            d["resultText"] = result["resultText"].toString();
        if (result.contains("analyzeMessage"))
            d["analyzeMessage"] = result["analyzeMessage"].toString();

        return true;
    }
};

#include "PythonStepPlugin.moc"
