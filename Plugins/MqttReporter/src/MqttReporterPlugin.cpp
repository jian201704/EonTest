#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include "eon/sdk/IStepPlugin.h"

class MqttReporterPlugin final : public QObject, public eon::sdk::IReporterPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_IREPORTERPLUGIN_IID FILE "mqttreporter.json")
    Q_INTERFACES(eon::sdk::IReporterPlugin)

public:
    QString id() const override { return "eon.reporter.mqtt"; }

    bool report(const eon::sdk::WorkflowContext& context, QString& errorMessage) override {
#if defined(EON_HAS_MQTT)
        // Qt MQTT 发送到 MES 主题
        QJsonObject payload;
        payload["workflowId"] = context.workflowId;
        payload["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        payload["data"] = QJsonObject::fromVariantMap(context.data);

        QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);

        const QString mqttTopic = context.data.value("_mqttTopic",
            QString("eontest/results/%1").arg(context.workflowId)).toString();
        const QString mqttBroker = context.data.value("_mqttBroker",
            "localhost").toString();
        const int mqttPort = context.data.value("_mqttPort", 1883).toInt();

        // Qt MQTT 需要客户端实例，此处简化（实际应由 HardwareManager 管理 MQTT 连接）
        Q_UNUSED(mqttTopic) Q_UNUSED(mqttBroker) Q_UNUSED(mqttPort) Q_UNUSED(json)
        qInfo() << "[MqttReporter] Would publish to" << mqttTopic << "@" << mqttBroker;
        return true;
#else
        Q_UNUSED(context)
    qWarning() << "[MqttReporter] Qt6::Mqtt not installed; skipping publish.";
    return true;
#endif
    }
};

#include "MqttReporterPlugin.moc"
