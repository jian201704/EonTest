#include "eon/infra/HardwareManager.h"

#include <QDir>
#include <QFileInfoList>
#include <QPluginLoader>

namespace eon::infra {

HardwareManager::HardwareManager(QObject* parent)
    : QObject(parent) {}

HardwareManager::~HardwareManager() {
    unloadAll();
}

// ============================================================================
// 驱动管理
// ============================================================================

bool HardwareManager::loadDrivers(const QString& pluginDirectory, QString* errorMessage) {
    const QDir dir(pluginDirectory);
    if (!dir.exists()) {
        if (errorMessage) {
            *errorMessage = QString("Driver plugin directory does not exist: %1").arg(pluginDirectory);
        }
        return false;
    }

    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    int loadedCount = 0;

    for (const QFileInfo& file : files) {
        // 每个驱动用独立 loader（Qt 插件卸载需要保留 loader）
        auto* loader = new QPluginLoader(file.absoluteFilePath(), this);
        QObject* instance = loader->instance();
        if (!instance) {
            loader->unload();
            delete loader;
            continue;
        }

        eon::sdk::IDriverPlugin* driver = nullptr;

        if (auto* busDriver = qobject_cast<eon::sdk::IBusDriver*>(instance)) {
            driver = busDriver;
        } else if (auto* protoLayer = qobject_cast<eon::sdk::IProtocolLayer*>(instance)) {
            driver = protoLayer;
        } else if (auto* devDriver = qobject_cast<eon::sdk::IDeviceDriver*>(instance)) {
            driver = devDriver;
        }

        if (!driver) {
            loader->unload();
            delete loader;
            continue;
        }

        // 检查元数据契约匹配
        const QJsonObject meta = loader->metaData().value("MetaData").toObject();
        const QString contractType = meta.value("contractType").toString();
        if (contractType != "driver" && contractType != "protocol" && contractType != "device") {
            loader->unload();
            delete loader;
            continue;
        }

        QString initError;
        if (!driver->initialize(initError)) {
            qWarning() << "Driver init failed:" << driver->id() << initError;
            loader->unload();
            delete loader;
            continue;
        }

        allDrivers_.append(driver);
        loadedCount++;
        emit driverLoaded(driver->id());
    }

    if (loadedCount == 0) {
        if (errorMessage) {
            *errorMessage = QString("No compatible driver plugins found in: %1").arg(pluginDirectory);
        }
        return false;
    }

    return true;
}

void HardwareManager::unloadAll() {
    closeAllBuses();

    for (auto it = devices_.begin(); it != devices_.end(); ++it) {
        emit deviceDisconnected(it.key());
        it->driver->disconnect();
        it->driver->shutdown();
    }
    devices_.clear();

    for (auto* driver : allDrivers_) {
        if (driver->isInitialized()) {
            driver->shutdown();
        }
        emit driverUnloaded(driver->id());
    }
    allDrivers_.clear();
}

QStringList HardwareManager::loadedDriverIds() const {
    QStringList ids;
    for (const auto* driver : allDrivers_) {
        ids.append(driver->id());
    }
    return ids;
}

// ============================================================================
// 总线管理
// ============================================================================

bool HardwareManager::openBus(const eon::sdk::BusConfig& config, QString* errorMessage) {
    if (config.busId.isEmpty()) {
        if (errorMessage) *errorMessage = "Bus ID is empty.";
        return false;
    }

    if (buses_.contains(config.busId)) {
        if (errorMessage) *errorMessage = QString("Bus '%1' is already open.").arg(config.busId);
        return false;
    }

    auto* busDriver = findBusDriverForType(config.type);
    if (!busDriver) {
        if (errorMessage) {
            *errorMessage = QString("No driver found for bus type '%1'.").arg(busTypeToString(config.type));
        }
        return false;
    }

    QString openError;
    if (!busDriver->open(config, openError)) {
        if (errorMessage) *errorMessage = openError;
        return false;
    }

    BusEntry entry;
    entry.busId = config.busId;
    entry.type = config.type;
    entry.driver = busDriver;

    buses_.insert(config.busId, entry);
    emit busOpened(config.busId, config.type);
    return true;
}

void HardwareManager::closeBus(const QString& busId) {
    if (!buses_.contains(busId)) return;

    BusEntry& entry = buses_[busId];
    unbindProtocols(busId);
    entry.driver->close();
    buses_.remove(busId);
    emit busClosed(busId);
}

void HardwareManager::closeAllBuses() {
    const QStringList ids = buses_.keys();
    for (const QString& id : ids) {
        closeBus(id);
    }
}

eon::sdk::IBusDriver* HardwareManager::busDriver(const QString& busId) const {
    return buses_.value(busId).driver;
}

bool HardwareManager::isBusOpen(const QString& busId) const {
    return buses_.contains(busId) && buses_.value(busId).driver->isOpen();
}

// ============================================================================
// 协议层管理
// ============================================================================

bool HardwareManager::bindProtocol(const QString& busId, eon::sdk::IProtocolLayer* protocol,
                                    QString* errorMessage) {
    if (!buses_.contains(busId)) {
        if (errorMessage) *errorMessage = QString("Bus '%1' is not open.").arg(busId);
        return false;
    }

    BusEntry& entry = buses_[busId];
    if (!entry.driver || !entry.driver->isOpen()) {
        if (errorMessage) *errorMessage = QString("Bus '%1' driver is not ready.").arg(busId);
        return false;
    }

    QString bindError;
    if (!protocol->bindBus(entry.driver, bindError)) {
        if (errorMessage) *errorMessage = bindError;
        return false;
    }

    entry.protocols.append(protocol);
    return true;
}

void HardwareManager::unbindProtocols(const QString& busId) {
    if (!buses_.contains(busId)) return;

    BusEntry& entry = buses_[busId];
    for (auto* proto : entry.protocols) {
        proto->unbindBus();
    }
    entry.protocols.clear();
}

// ============================================================================
// 设备管理
// ============================================================================

bool HardwareManager::connectDevice(const QString& driverId, const QVariantMap& config,
                                     QString* errorMessage) {
    if (devices_.contains(driverId)) {
        if (errorMessage) *errorMessage = QString("Device '%1' is already connected.").arg(driverId);
        return false;
    }

    auto* driver = dynamic_cast<eon::sdk::IDeviceDriver*>(findDriverById(driverId));
    if (!driver) {
        if (errorMessage) *errorMessage = QString("Device driver '%1' not found.").arg(driverId);
        return false;
    }

    QString connError;
    if (!driver->connect(config, connError)) {
        if (errorMessage) *errorMessage = connError;
        return false;
    }

    DeviceEntry entry;
    entry.driverId = driverId;
    entry.driver = driver;
    devices_.insert(driverId, entry);
    emit deviceConnected(driverId);
    return true;
}

void HardwareManager::disconnectDevice(const QString& driverId) {
    if (!devices_.contains(driverId)) return;

    DeviceEntry& entry = devices_[driverId];
    entry.driver->disconnect();
    devices_.remove(driverId);
    emit deviceDisconnected(driverId);
}

// ============================================================================
// 状态查询
// ============================================================================

QVariantMap HardwareManager::statusSnapshot() const {
    QVariantMap snapshot;

    QVariantMap busStatus;
    for (auto it = buses_.constBegin(); it != buses_.constEnd(); ++it) {
        QVariantMap info;
        info["type"] = eon::sdk::busTypeToString(it->type);
        info["open"] = it->driver ? it->driver->isOpen() : false;
        info["protocols"] = it->protocols.size();
        busStatus[it.key()] = info;
    }
    snapshot["buses"] = busStatus;

    QVariantMap deviceStatus;
    for (auto it = devices_.constBegin(); it != devices_.constEnd(); ++it) {
        deviceStatus[it.key()] = it->driver->isConnected();
    }
    snapshot["devices"] = deviceStatus;

    snapshot["driverCount"] = static_cast<int>(allDrivers_.size());
    return snapshot;
}

// ============================================================================
// 私有方法
// ============================================================================

eon::sdk::IBusDriver* HardwareManager::findBusDriverForType(eon::sdk::BusType type) const {
    for (auto* driver : allDrivers_) {
        auto* busDriver = dynamic_cast<eon::sdk::IBusDriver*>(driver);
        if (busDriver && busDriver->busType() == type) {
            return busDriver;
        }
    }
    return nullptr;
}

eon::sdk::IDriverPlugin* HardwareManager::findDriverById(const QString& driverId) const {
    for (auto* driver : allDrivers_) {
        if (driver->id() == driverId) {
            return driver;
        }
    }
    return nullptr;
}

} // namespace eon::infra
