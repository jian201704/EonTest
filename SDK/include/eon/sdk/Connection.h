#pragma once

#include <QString>
#include <QVector>
#include <QVariantMap>

namespace eon::sdk {

/// <summary>
/// 物理端口（对标 OpenTAP Port）。
/// 仪器/DUT 上的物理连接端点。
/// </summary>
struct Port {
    QString name;              // 端口名称，如 "RF Out", "GPIO 1"
    QString direction;         // "input" | "output" | "bidirectional"
    QString connectorType;     // "SMA", "BNC", "N-type", "banana", "D-sub 9" 等
    QString description;       // 描述信息

    QVariantMap toVariantMap() const {
        return {
            {"name", name},
            {"direction", direction},
            {"connectorType", connectorType},
            {"description", description}
        };
    }
};

/// <summary>
/// 物理连接（对标 OpenTAP Connection）。
/// 连接两个 Port，建模线缆等物理介质。
/// </summary>
class Connection {
public:
    virtual ~Connection() = default;

    /// 连接名称
    virtual QString name() const = 0;

    /// 连接的两个端口
    virtual Port portA() const = 0;
    virtual Port portB() const = 0;

    /// 序列化为 JSON
    virtual QVariantMap toVariantMap() const {
        return {
            {"name", name()},
            {"portA", portA().toVariantMap()},
            {"portB", portB().toVariantMap()}
        };
    }
};

/// <summary>
/// RF 连接（对标 OpenTAP RfConnection）。
/// 包含线缆损耗点列表，支持频率相关损耗补偿。
/// </summary>
struct LossPoint {
    double frequencyHz = 0.0;  // 频率 (Hz)
    double lossDb = 0.0;       // 损耗 (dB)
};

class RfConnection : public Connection {
public:
    RfConnection(const QString& name, const Port& a, const Port& b)
        : name_(name), portA_(a), portB_(b) {}

    QString name() const override { return name_; }
    Port portA() const override { return portA_; }
    Port portB() const override { return portB_; }

    /// 线缆损耗点列表
    QVector<LossPoint> cableLoss() const { return cableLoss_; }
    void setCableLoss(const QVector<LossPoint>& loss) { cableLoss_ = loss; }

    /// 在指定频率处插值计算线缆损耗
    double lossAt(double frequencyHz) const;

    QVariantMap toVariantMap() const override {
        auto m = Connection::toVariantMap();
        QVariantList cl;
        for (const auto& lp : cableLoss_) {
            cl.append(QVariantMap{
                {"frequencyHz", lp.frequencyHz},
                {"lossDb", lp.lossDb}
            });
        }
        m["cableLoss"] = cl;
        return m;
    }

private:
    QString name_;
    Port portA_;
    Port portB_;
    QVector<LossPoint> cableLoss_;
};

} // namespace eon::sdk
