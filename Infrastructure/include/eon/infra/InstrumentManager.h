#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>

namespace eon::sdk { class InstrumentDriver; class IBackend; }
namespace eon::infra {

// ============================================================================
// InstrumentManager — 仪器注册表（单例）
//
// 程序启动时从 Excel Sheet 2 或 instruments JSON 段加载设备配置，
// 创建对应的 Backend + Driver 实例并注册。
//
// 使用方式：
//   InstrumentManager::instance().loadFromJson(instrumentsJson);
//   auto* psu = InstrumentManager::instance().get("psu_main");
//   psu->execute("on", {{"voltage", 3.3}}, error);
// ============================================================================
class InstrumentManager {
public:
    struct Entry {
        QString name;       // 逻辑名，如 "psu_main"
        QString driverType; // 驱动类型，如 "power.supply"
        QString backendType;// 后端类型，如 "serial", "qprocess_python"
        QVariantMap config; // 后端配置（端口、波特率、脚本路径等）
    };

    static InstrumentManager& instance();

    /// 从 JSON 加载仪器注册表
    bool loadFromJson(const QJsonObject& instruments, const QJsonObject& globalConfig, QString& error);

    /// 从 QVariantMap 加载
    bool loadFromConfig(const QList<Entry>& entries, const QVariantMap& globalConfig, QString& error);

    /// 获取仪器驱动实例（nullptr 表示未注册或连接失败）
    eon::sdk::InstrumentDriver* get(const QString& name) const;

    /// 按驱动类型查找
    QList<eon::sdk::InstrumentDriver*> findByType(const QString& driverType) const;

    /// 连接所有仪器
    bool connectAll(QString& error);

    /// 断开所有仪器
    void disconnectAll();

    /// 全部名称
    QStringList names() const;

    /// 清空注册表
    void clear();

private:
    InstrumentManager() = default;
    ~InstrumentManager();

    struct Instance {
        std::unique_ptr<eon::sdk::IBackend> backend;
        std::unique_ptr<eon::sdk::InstrumentDriver> driver;
    };
    QHash<QString, Instance*> instances_;
    QList<Entry> entries_;
};

} // namespace eon::infra
