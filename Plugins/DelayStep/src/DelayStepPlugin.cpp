#include <QObject>
#include <QThread>
#include <QVariantMap>

#include "eon/sdk/IStepPlugin.h"

// ============================================================================
// DelayStepPlugin — 延时等待插件
//
// context.data 参数：
//   delayMs  - 延时毫秒数（默认 1000）
// ============================================================================
class DelayStepPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "delaystep.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override {
        return "delay";
    }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        Q_UNUSED(errorMessage)
        const int delayMs = context.data.value("delayMs", 1000).toInt();
        QThread::msleep(delayMs);
        return true;
    }
};

#include "DelayStepPlugin.moc"
