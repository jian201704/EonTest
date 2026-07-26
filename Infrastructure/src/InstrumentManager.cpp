#include <eon/infra/InstrumentManager.h>
#include <eon/infra/PowerSupplyDriver.h>
#include <eon/infra/MultimeterDriver.h>
#include <eon/infra/SerialBackend.h>
#include <eon/infra/PythonProcessBackend.h>
#include <eon/sdk/IBackend.h>
#include <eon/sdk/InstrumentDriver.h>

#include <QJsonObject>
#include <QJsonArray>

namespace eon::infra {

InstrumentManager& InstrumentManager::instance() {
    static InstrumentManager mgr;
    return mgr;
}

InstrumentManager::~InstrumentManager() { disconnectAll(); }

void InstrumentManager::clear() {
    disconnectAll();
    for (auto* inst : instances_) delete inst;
    instances_.clear();
    entries_.clear();
}

bool InstrumentManager::loadFromJson(const QJsonObject& instruments, const QJsonObject& globalConfig, QString& error) {
    clear();

    for (auto it = instruments.begin(); it != instruments.end(); ++it) {
        const QString name = it.key();
        const QJsonObject obj = it.value().toObject();
        Entry entry;
        entry.name = name;
        entry.driverType = obj.value("driver").toString();
        entry.backendType = obj.value("backend").toString();
        entry.config = obj.toVariantMap();
        entries_.append(entry);
    }

    return connectAll(error);
}

bool InstrumentManager::loadFromConfig(const QList<Entry>& entries, const QVariantMap& globalConfig, QString& error) {
    Q_UNUSED(globalConfig);
    clear();
    entries_ = entries;
    return connectAll(error);
}

bool InstrumentManager::connectAll(QString& error) {
    for (const auto& entry : entries_) {
        auto* inst = new Instance();

        // 1) 创建 Backend
        if (entry.backendType == "serial") {
            inst->backend = std::make_unique<SerialBackend>();
        } else if (entry.backendType == "python" || entry.backendType == "qprocess_python") {
            inst->backend = std::make_unique<PythonProcessBackend>();
        } else {
            delete inst;
            error = QString("Unknown backend type '%1' for instrument '%2'")
                .arg(entry.backendType, entry.name);
            return false;
        }

        // 2) 创建 Driver
        if (entry.driverType == "power.supply") {
            inst->driver = std::make_unique<PowerSupplyDriver>();
        } else if (entry.driverType == "measure.voltage") {
            inst->driver = std::make_unique<MultimeterDriver>();
        } else {
            delete inst;
            error = QString("Unknown driver type '%1' for instrument '%2'")
                .arg(entry.driverType, entry.name);
            return false;
        }

        // 3) 连接
        if (!inst->driver->open(inst->backend.get(), entry.config, error)) {
            delete inst;
            return false;
        }

        instances_.insert(entry.name, inst);
    }
    return true;
}

eon::sdk::InstrumentDriver* InstrumentManager::get(const QString& name) const {
    auto it = instances_.find(name);
    return it != instances_.end() ? it.value()->driver.get() : nullptr;
}

QList<eon::sdk::InstrumentDriver*> InstrumentManager::findByType(const QString& driverType) const {
    QList<eon::sdk::InstrumentDriver*> result;
    for (auto& inst : instances_) {
        if (inst && inst->driver && inst->driver->driverType() == driverType)
            result.append(inst->driver.get());
    }
    return result;
}

QStringList InstrumentManager::names() const {
    return instances_.keys();
}

void InstrumentManager::disconnectAll() {
    for (auto& inst : instances_) {
        if (inst->driver) inst->driver->close();
    }
    instances_.clear();
}

} // namespace eon::infra
