#include <QObject>
#include <QVariantMap>

#include "eon/sdk/IStepPlugin.h"
#include "eon/sdk/IDriverPlugin.h" // BusFrame

class CanSendPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "can-send.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override { return "can.send"; }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        auto& d = context.data;
        const bool virt = d.value("virtualMode", false).toBool();

        eon::sdk::BusFrame frame;
        frame.id = static_cast<quint32>(d.value("canId", d.value("id", 0x100)).toUInt());
        frame.isExtended = d.value("extended", false).toBool();
        frame.isRemote = d.value("remote", false).toBool();

        const QString dataStr = d.value("data", "").toString();
        if (!dataStr.isEmpty()) {
            frame.data = QByteArray::fromHex(dataStr.toLatin1());
        } else {
            // 从 values 列表构造
            const QVariantList vals = d.value("values").toList();
            for (const auto& v : vals)
                frame.data.append(static_cast<char>(v.toUInt()));
        }

        if (virt) {
            // 虚拟模式：记录到 context
            d.insert("can.sent", true);
            d.insert("can.frameId", static_cast<qulonglong>(frame.id));
            d.insert("can.data", frame.data.toHex(' '));
            d.insert("can.bytes", frame.data.size());
            return true;
        }

        // 真实硬件 — 通过 InstrumentManager 获取 CAN 后端
        // 后端类型从 context 配置读取："pcan" / "socketcan" / "serial"（串口转CAN）
        const QString backendType = d.value("canBackend", d.value("backend", "pcan")).toString();
        const QString iface = d.value("interface", d.value("canIface", "PCAN_USBBUS1")).toString();

        // 这里通过 IBackend 打开 CAN 通道
        // 由于 CAN 需要特殊 API，此处留接口供集成方实现
        d.insert("can.backend", backendType);
        d.insert("can.interface", iface);

        // 提示：需要安装 PCANBasic / SocketCAN 库
        errorMessage = QString("[can.send] 真实 CAN 硬件需要 PCANBasic/SocketCAN SDK。"
                               "当前为桩实现，请在 Step 中设置 virtualMode=true 跳过。");
        return false;
    }
};

#include "CanSendPlugin.moc"
