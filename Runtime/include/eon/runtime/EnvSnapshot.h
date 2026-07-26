#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace eon::runtime {

/// <summary>
/// 运行环境快照（对应章节 16.5）。
/// 在 PrePlanRun 开始时收集并写入 reports/run-<id>/env_snapshot.json。
/// 保证每次 Plan 执行的可重现性。
/// </summary>
struct EnvSnapshot {
    QString runId;
    QString timestamp;
    QString gitCommit;
    QString os;
    QString compilerVersion;
    QString qtVersion;
    QString eonVersion;
    QStringList pluginVersions;
    QStringList resourceFirmware;

    /// 转为 JSON
    QJsonObject toJson() const {
        return {
            {"runId", runId},
            {"timestamp", timestamp},
            {"gitCommit", gitCommit},
            {"os", os},
            {"compiler", compilerVersion},
            {"qtVersion", qtVersion},
            {"eonVersion", eonVersion},
            {"pluginVersions", QJsonArray::fromStringList(pluginVersions)},
            {"resourceFirmware", QJsonArray::fromStringList(resourceFirmware)}
        };
    }
};

/// <summary>
/// 捕获当前环境快照
/// </summary>
EnvSnapshot captureEnvSnapshot(const QString& runId);

/// <summary>
/// 将快照写入文件
/// </summary>
bool writeEnvSnapshot(const QString& filePath, const EnvSnapshot& snapshot);

} // namespace eon::runtime
