#include <QObject>
#include <QVariantMap>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "eon/sdk/IStepPlugin.h"
#include "eon/sdk/IDriverPlugin.h"
#include "eon/infra/ResponseDecoder.h"

namespace {
QVariantList loadDecodeProfile(const QVariant& value) {
    const QString path = value.toString().trimmed();
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (document.isArray()) return document.array().toVariantList();
    if (document.isObject() && document.object().value("measurements").isArray())
        return document.object().value("measurements").toArray().toVariantList();
    return {};
}

bool decodeCanPayload(const QByteArray& payload, const QVariantMap& data,
                      QVariantMap& output, QString& errorMessage) {
    QVariantList specs = data.value("decodeSpecs").toList();
    if (specs.isEmpty()) specs = loadDecodeProfile(data.value("decodeProfile"));
    const auto single = eon::infra::DecodeSpec::fromVariantMap(data);
    if (specs.isEmpty() && !single.hasExplicitDecode()) return true;
    const auto decoded = specs.isEmpty()
        ? eon::infra::ResponseDecoder::decode(payload, single)
        : eon::infra::ResponseDecoder::decodeMany(payload, specs);
    output["rawResponseHex"] = payload.toHex(' ').toUpper();
    output["resultItems"] = decoded.toVariantList();
    output["measuredSamples"] = decoded.toVariantList();
    if (!decoded.success) {
        errorMessage = QString("CAN response decode failed: %1").arg(decoded.errorMessage);
        return false;
    }
    if (!decoded.measurements.isEmpty()) {
        const auto& first = decoded.measurements.first();
        output["measuredValue"] = first.value;
        output["measuredUnit"] = first.unit;
        output["measurementName"] = first.name;
    }
    return true;
}
}

class CanReceivePlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "can-receive.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override { return "can.receive"; }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        auto& d = context.data;
        const bool virt = d.value("virtualMode", false).toBool();
        const uint32_t expectedId = static_cast<uint32_t>(d.value("canId", d.value("id", 0x100)).toUInt());
        const QString filterStr = d.value("filter", d.value("dataMatch", "")).toString();
        const int timeoutMs = d.value("timeoutMs", d.value("timeout", 5000)).toInt();

        if (virt) {
            // 虚拟模式：返回模拟帧
            eon::sdk::BusFrame frame;
            frame.id = expectedId;
            frame.data = QByteArray::fromHex("62 02 01 0A 00 00 00 00");
            d.insert("can.received", true);
            d.insert("can.frameId", static_cast<qulonglong>(frame.id));
            d.insert("can.data", frame.data.toHex(' '));
            d.insert("can.bytes", frame.data.size());
            d.insert("measuredValue", frame.data.size());
            d.insert("measuredUnit", "bytes");
            if (frame.data.size() >= 2) {
                d.insert("can.responseCode",
                         QString("0x%1").arg(static_cast<uint8_t>(frame.data[1]), 2, 16, QChar('0')));
            }
            if (!decodeCanPayload(frame.data, d, d, errorMessage)) return false;
            return true;
        }

        // 真实硬件 — 需要 CAN 后端
        d.insert("can.backend", d.value("canBackend", "pcan").toString());
        errorMessage = QString("[can.receive] 真实 CAN 硬件需要 PCANBasic/SocketCAN SDK。"
                               "请在 Step 中设置 virtualMode=true 跳过。");
        return false;
    }
};

#include "CanReceivePlugin.moc"
