#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace eon::studio {

// ============================================================================
// DutEntry — 单个 DUT 配置条目
// ============================================================================
struct DutEntry {
    QString pluginId;        // DUT 插件 ID（如 "simple.dut"）
    QString dutId;           // 序列号
    QString modelName;       // 型号
    QString firmwareVersion; // 固件版本
    QString description;     // 描述
    QString connectionPort;  // 连接端口（COM5 / TCP:192.168.1.1:5025）
    bool enabled = true;     // 是否启用

    QJsonObject toJson() const {
        return {
            {"pluginId", pluginId},
            {"dutId", dutId},
            {"modelName", modelName},
            {"firmwareVersion", firmwareVersion},
            {"description", description},
            {"connectionPort", connectionPort},
            {"enabled", enabled}
        };
    }

    static DutEntry fromJson(const QJsonObject& obj) {
        return {
            obj.value("pluginId").toString(),
            obj.value("dutId").toString(),
            obj.value("modelName").toString(),
            obj.value("firmwareVersion").toString(),
            obj.value("description").toString(),
            obj.value("connectionPort").toString(),
            obj.value("enabled").toBool(true)
        };
    }
};

// ============================================================================
// InstrumentEntry — 单个仪器配置条目
// ============================================================================
struct InstrumentEntry {
    QString pluginId;        // 仪器插件 ID（如 "dmm.plugin"）
    QString instrumentName;  // 仪器名称（用户可自定义）
    QString visaAddress;     // VISA 地址（如 "TCPIP::192.168.1.10::INSTR"）
    int ioTimeoutMs = 2000;  // I/O 超时(ms)
    QString description;
    bool enabled = true;

    QJsonObject toJson() const {
        return {
            {"pluginId", pluginId},
            {"instrumentName", instrumentName},
            {"visaAddress", visaAddress},
            {"ioTimeoutMs", ioTimeoutMs},
            {"description", description},
            {"enabled", enabled}
        };
    }

    static InstrumentEntry fromJson(const QJsonObject& obj) {
        return {
            obj.value("pluginId").toString(),
            obj.value("instrumentName").toString(),
            obj.value("visaAddress").toString(),
            obj.value("ioTimeoutMs").toInt(2000),
            obj.value("description").toString(),
            obj.value("enabled").toBool(true)
        };
    }
};

// ============================================================================
// DutListModel — DUT 列表 QAbstractListModel（供 QML ListView 绑定）
// ============================================================================
class DutListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PluginIdRole = Qt::UserRole + 1,
        DutIdRole, ModelNameRole, FirmwareVersionRole,
        DescriptionRole, ConnectionPortRole, EnabledRole
    };

    explicit DutListModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setEntries(const QVector<DutEntry>& entries);
    QVector<DutEntry> entries() const { return entries_; }

    Q_INVOKABLE void addEntry();
    Q_INVOKABLE void removeEntry(int row);

private:
    QVector<DutEntry> entries_;
};

// ============================================================================
// InstrumentListModel — 仪器列表 QAbstractListModel
// ============================================================================
class InstrumentListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PluginIdRole = Qt::UserRole + 1,
        InstrumentNameRole, VisaAddressRole, IoTimeoutRole,
        DescriptionRole, EnabledRole
    };

    explicit InstrumentListModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setEntries(const QVector<InstrumentEntry>& entries);
    QVector<InstrumentEntry> entries() const { return entries_; }

    Q_INVOKABLE void addEntry();
    Q_INVOKABLE void removeEntry(int row);

private:
    QVector<InstrumentEntry> entries_;
};

// ============================================================================
// BenchSettings — Bench 配置管理器（对标 OpenTAP Settings > Bench）
//
// 管理 DUT 和 Instrument 的配置列表，持久化到 JSON 文件。
// 通过 Q_PROPERTY 暴露给 QML，支持 Profile 切换。
// ============================================================================
class BenchSettings : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString configFilePath READ configFilePath WRITE setConfigFilePath NOTIFY configFilePathChanged)
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)
    Q_PROPERTY(QStringList availableProfiles READ availableProfiles NOTIFY profilesChanged)
    Q_PROPERTY(QObject* dutModel READ dutModel CONSTANT)
    Q_PROPERTY(QObject* instrumentModel READ instrumentModel CONSTANT)

public:
    explicit BenchSettings(QObject* parent = nullptr);

    // 文件路径
    QString configFilePath() const;
    void setConfigFilePath(const QString& path);

    // Profile 管理
    QString currentProfile() const { return currentProfile_; }
    void setCurrentProfile(const QString& profile);
    QStringList availableProfiles() const;

    // 数据模型（QML ListView 直接绑定）
    QObject* dutModel() { return &dutModel_; }
    QObject* instrumentModel() { return &instrumentModel_; }

public slots:
    /// 加载配置（从文件或默认目录）
    bool load();
    /// 保存配置到文件
    bool save();
    /// 创建新 Profile
    void createProfile(const QString& profileName);
    /// 删除当前 Profile
    void deleteCurrentProfile();
    /// 重置为默认配置
    void resetToDefaults();

signals:
    void configFilePathChanged();
    void currentProfileChanged();
    void profilesChanged();
    void loaded();
    void saved();
    void errorOccurred(const QString& message);

private:
    QString resolveConfigPath() const;

    DutListModel dutModel_;
    InstrumentListModel instrumentModel_;
    QString configFilePath_;
    QString currentProfile_ = "default";
};

} // namespace eon::studio
