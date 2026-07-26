#include "eon/studio/BenchSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace eon::studio {

// ============================================================================
// DutListModel
// ============================================================================

DutListModel::DutListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int DutListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : entries_.size();
}

QVariant DutListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= entries_.size()) return {};
    const auto& e = entries_.at(index.row());
    switch (role) {
    case PluginIdRole:        return e.pluginId;
    case DutIdRole:           return e.dutId;
    case ModelNameRole:       return e.modelName;
    case FirmwareVersionRole: return e.firmwareVersion;
    case DescriptionRole:     return e.description;
    case ConnectionPortRole:  return e.connectionPort;
    case EnabledRole:         return e.enabled;
    default: return {};
    }
}

bool DutListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= entries_.size()) return false;
    auto& e = entries_[index.row()];
    switch (role) {
    case PluginIdRole:        e.pluginId = value.toString(); break;
    case DutIdRole:           e.dutId = value.toString(); break;
    case ModelNameRole:       e.modelName = value.toString(); break;
    case FirmwareVersionRole: e.firmwareVersion = value.toString(); break;
    case DescriptionRole:     e.description = value.toString(); break;
    case ConnectionPortRole:  e.connectionPort = value.toString(); break;
    case EnabledRole:         e.enabled = value.toBool(); break;
    default: return false;
    }
    emit dataChanged(index, index, {role});
    return true;
}

QHash<int, QByteArray> DutListModel::roleNames() const {
    return {
        {PluginIdRole, "pluginId"},
        {DutIdRole, "dutId"},
        {ModelNameRole, "modelName"},
        {FirmwareVersionRole, "firmwareVersion"},
        {DescriptionRole, "description"},
        {ConnectionPortRole, "connectionPort"},
        {EnabledRole, "enabled"}
    };
}

Qt::ItemFlags DutListModel::flags(const QModelIndex& index) const {
    return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

void DutListModel::setEntries(const QVector<DutEntry>& entries) {
    beginResetModel();
    entries_ = entries;
    endResetModel();
}

void DutListModel::addEntry() {
    beginInsertRows({}, entries_.size(), entries_.size());
    entries_.append({"", "SN-New", "", "", "", "", true});
    endInsertRows();
}

void DutListModel::removeEntry(int row) {
    if (row < 0 || row >= entries_.size()) return;
    beginRemoveRows({}, row, row);
    entries_.removeAt(row);
    endRemoveRows();
}

// ============================================================================
// InstrumentListModel
// ============================================================================

InstrumentListModel::InstrumentListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int InstrumentListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : entries_.size();
}

QVariant InstrumentListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= entries_.size()) return {};
    const auto& e = entries_.at(index.row());
    switch (role) {
    case PluginIdRole:       return e.pluginId;
    case InstrumentNameRole: return e.instrumentName;
    case VisaAddressRole:    return e.visaAddress;
    case IoTimeoutRole:      return e.ioTimeoutMs;
    case DescriptionRole:    return e.description;
    case EnabledRole:        return e.enabled;
    default: return {};
    }
}

bool InstrumentListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= entries_.size()) return false;
    auto& e = entries_[index.row()];
    switch (role) {
    case PluginIdRole:       e.pluginId = value.toString(); break;
    case InstrumentNameRole: e.instrumentName = value.toString(); break;
    case VisaAddressRole:    e.visaAddress = value.toString(); break;
    case IoTimeoutRole:      e.ioTimeoutMs = value.toInt(); break;
    case DescriptionRole:    e.description = value.toString(); break;
    case EnabledRole:        e.enabled = value.toBool(); break;
    default: return false;
    }
    emit dataChanged(index, index, {role});
    return true;
}

QHash<int, QByteArray> InstrumentListModel::roleNames() const {
    return {
        {PluginIdRole, "pluginId"},
        {InstrumentNameRole, "instrumentName"},
        {VisaAddressRole, "visaAddress"},
        {IoTimeoutRole, "ioTimeout"},
        {DescriptionRole, "description"},
        {EnabledRole, "enabled"}
    };
}

Qt::ItemFlags InstrumentListModel::flags(const QModelIndex& index) const {
    return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

void InstrumentListModel::setEntries(const QVector<InstrumentEntry>& entries) {
    beginResetModel();
    entries_ = entries;
    endResetModel();
}

void InstrumentListModel::addEntry() {
    beginInsertRows({}, entries_.size(), entries_.size());
    entries_.append({"", "New Instrument", "TCPIP::192.168.1.1::INSTR", 2000, "", true});
    endInsertRows();
}

void InstrumentListModel::removeEntry(int row) {
    if (row < 0 || row >= entries_.size()) return;
    beginRemoveRows({}, row, row);
    entries_.removeAt(row);
    endRemoveRows();
}

// ============================================================================
// BenchSettings
// ============================================================================

BenchSettings::BenchSettings(QObject* parent)
    : QObject(parent) {}

QString BenchSettings::configFilePath() const {
    return configFilePath_;
}

void BenchSettings::setConfigFilePath(const QString& path) {
    if (configFilePath_ != path) {
        configFilePath_ = path;
        emit configFilePathChanged();
    }
}

QString BenchSettings::resolveConfigPath() const {
    if (!configFilePath_.isEmpty()) return configFilePath_;
    // 默认路径：可执行文件同目录下的 bench.json
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
           + "/bench.json";
}

QStringList BenchSettings::availableProfiles() const {
    QString path = resolveConfigPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {"default"};
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();
    QJsonObject profiles = root.value("profiles").toObject();
    return profiles.keys();
}

void BenchSettings::setCurrentProfile(const QString& profile) {
    if (currentProfile_ != profile) {
        currentProfile_ = profile;
        emit currentProfileChanged();
        load();
    }
}

bool BenchSettings::load() {
    QString path = resolveConfigPath();
    QFile file(path);
    if (!file.exists()) {
        // 文件不存在时加载默认值
        QVector<DutEntry> defaultDuts = {
            {"simple.dut", "SN-001", "E36313A", "1.0", "Demo DUT 1", "COM5", true}
        };
        QVector<InstrumentEntry> defaultInstruments = {
            {"dmm.plugin", "DMM", "TCPIP::192.168.1.10::INSTR", 2000, "Digital Multimeter", true},
            {"power.plugin", "Power Supply", "TCPIP::192.168.1.11::INSTR", 2000, "Bench Power Supply", true}
        };
        dutModel_.setEntries(defaultDuts);
        instrumentModel_.setEntries(defaultInstruments);
        emit loaded();
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QString("Cannot open bench config: %1").arg(path));
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred(QString("JSON parse error: %1").arg(parseError.errorString()));
        return false;
    }

    QJsonObject root = doc.object();
    QJsonObject profileData = root.value("profiles").toObject().value(currentProfile_).toObject();

    // 加载 DUT 列表
    QVector<DutEntry> duts;
    QJsonArray dutsArr = profileData.value("duts").toArray();
    for (const auto& v : dutsArr)
        duts.append(DutEntry::fromJson(v.toObject()));
    dutModel_.setEntries(duts);

    // 加载仪器列表
    QVector<InstrumentEntry> instruments;
    QJsonArray instrumentsArr = profileData.value("instruments").toArray();
    for (const auto& v : instrumentsArr)
        instruments.append(InstrumentEntry::fromJson(v.toObject()));
    instrumentModel_.setEntries(instruments);

    emit loaded();
    return true;
}

bool BenchSettings::save() {
    QString path = resolveConfigPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // 读取现有文件（保留其他 profile）
    QJsonObject root;
    {
        QFile readFile(path);
        if (readFile.open(QIODevice::ReadOnly)) {
            root = QJsonDocument::fromJson(readFile.readAll()).object();
            readFile.close();
        }
    }

    // 构建当前 profile 数据
    QJsonObject profileData;
    {
        QJsonArray dutsArr;
        for (const auto& d : dutModel_.entries())
            dutsArr.append(d.toJson());
        profileData["duts"] = dutsArr;

        QJsonArray instArr;
        for (const auto& i : instrumentModel_.entries())
            instArr.append(i.toJson());
        profileData["instruments"] = instArr;
    }

    QJsonObject profiles = root.value("profiles").toObject();
    profiles[currentProfile_] = profileData;
    root["profiles"] = profiles;
    root["lastModified"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(QString("Cannot write bench config: %1").arg(path));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    emit saved();
    emit profilesChanged();
    return true;
}

void BenchSettings::createProfile(const QString& profileName) {
    currentProfile_ = profileName;
    QVector<DutEntry> emptyDuts;
    QVector<InstrumentEntry> emptyInsts;
    dutModel_.setEntries(emptyDuts);
    instrumentModel_.setEntries(emptyInsts);
    emit currentProfileChanged();
    save(); // 立即持久化
}

void BenchSettings::deleteCurrentProfile() {
    if (currentProfile_ == "default") {
        emit errorOccurred("Cannot delete the default profile.");
        return;
    }

    QString path = resolveConfigPath();
    QFile readFile(path);
    QJsonObject root;
    if (readFile.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(readFile.readAll()).object();
        readFile.close();
    }

    QJsonObject profiles = root.value("profiles").toObject();
    profiles.remove(currentProfile_);
    root["profiles"] = profiles;

    QFile writeFile(path);
    writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    writeFile.write(QJsonDocument(root).toJson());
    writeFile.close();

    currentProfile_ = "default";
    emit currentProfileChanged();
    emit profilesChanged();
    load();
}

void BenchSettings::resetToDefaults() {
    QVector<DutEntry> defaultDuts = {
        {"simple.dut", "SN-001", "E36313A", "1.0", "Demo DUT", "COM5", true}
    };
    QVector<InstrumentEntry> defaultInstruments = {
        {"dmm.plugin", "DMM", "TCPIP::192.168.1.10::INSTR", 2000, "Digital Multimeter", true}
    };
    dutModel_.setEntries(defaultDuts);
    instrumentModel_.setEntries(defaultInstruments);
}

} // namespace eon::studio
