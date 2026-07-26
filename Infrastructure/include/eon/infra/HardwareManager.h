#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include "eon/sdk/IDriverPlugin.h"

namespace eon::infra {

// ============================================================================
// HardwareManager — 硬件驱动生命周期管理
// 负责驱动插件的加载、初始化、总线绑定、协议栈绑定
// ============================================================================
class HardwareManager : public QObject {
    Q_OBJECT

public:
    explicit HardwareManager(QObject* parent = nullptr);
    ~HardwareManager() override;

    // --- 驱动管理 ---

    /// 从指定目录加载所有驱动插件
    bool loadDrivers(const QString& pluginDirectory, QString* errorMessage = nullptr);

    /// 卸载所有驱动
    void unloadAll();

    /// 获取已加载的驱动 ID 列表
    QStringList loadedDriverIds() const;

    // --- 总线管理 ---

    /// 打开总线（自动查找匹配的驱动）
    bool openBus(const eon::sdk::BusConfig& config, QString* errorMessage = nullptr);

    /// 关闭指定总线
    void closeBus(const QString& busId);

    /// 关闭所有总线
    void closeAllBuses();

    /// 获取总线驱动（用于协议层绑定）
    eon::sdk::IBusDriver* busDriver(const QString& busId) const;

    /// 总线是否已打开
    bool isBusOpen(const QString& busId) const;

    // --- 协议层管理 ---

    /// 在指定总线上绑定协议层
    bool bindProtocol(const QString& busId, eon::sdk::IProtocolLayer* protocol,
                      QString* errorMessage = nullptr);

    /// 解绑总线上所有协议
    void unbindProtocols(const QString& busId);

    // --- 设备管理 ---

    /// 连接设备
    bool connectDevice(const QString& driverId, const QVariantMap& config,
                       QString* errorMessage = nullptr);

    /// 断开设备
    void disconnectDevice(const QString& driverId);

    // --- 状态查询 ---

    /// 获取全局硬件状态快照
    QVariantMap statusSnapshot() const;

signals:
    void driverLoaded(const QString& driverId);
    void driverUnloaded(const QString& driverId);
    void busOpened(const QString& busId, eon::sdk::BusType type);
    void busClosed(const QString& busId);
    void busError(const QString& busId, const QString& error);
    void deviceConnected(const QString& driverId);
    void deviceDisconnected(const QString& driverId);

private:
    struct BusEntry {
        QString busId;
        eon::sdk::BusType type = eon::sdk::BusType::Virtual;
        eon::sdk::IBusDriver* driver = nullptr;
        QList<eon::sdk::IProtocolLayer*> protocols;
    };

    struct DeviceEntry {
        QString driverId;
        eon::sdk::IDeviceDriver* driver = nullptr;
    };

    eon::sdk::IBusDriver* findBusDriverForType(eon::sdk::BusType type) const;
    eon::sdk::IDriverPlugin* findDriverById(const QString& driverId) const;

    QList<eon::sdk::IDriverPlugin*> allDrivers_;
    QHash<QString, BusEntry> buses_;        // busId -> BusEntry
    QHash<QString, DeviceEntry> devices_;    // driverId -> DeviceEntry
};

} // namespace eon::infra
