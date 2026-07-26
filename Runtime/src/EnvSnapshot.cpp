#include "eon/runtime/EnvSnapshot.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPluginLoader>
#include <QProcess>
#include <QSysInfo>
#include <QDateTime>

namespace eon::runtime {

EnvSnapshot captureEnvSnapshot(const QString& runId)
{
    EnvSnapshot snap;
    snap.runId = runId;
    snap.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // Git commit
    QProcess git;
    git.start("git", {"rev-parse", "--short", "HEAD"});
    if (git.waitForFinished(3000) && git.exitCode() == 0) {
        snap.gitCommit = QString::fromUtf8(git.readAllStandardOutput()).trimmed();
    } else {
        snap.gitCommit = "unknown";
    }

    // OS
    snap.os = QSysInfo::prettyProductName();

    // Compiler
#if defined(_MSC_VER)
    snap.compilerVersion = QString("MSVC %1").arg(_MSC_VER);
#elif defined(__clang__)
    snap.compilerVersion = QString("Clang %1").arg(__clang_version__);
#elif defined(__GNUC__)
    snap.compilerVersion = QString("GCC %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
    snap.compilerVersion = "unknown";
#endif

    // Qt version
    snap.qtVersion = QString::fromLatin1(qVersion());

    // EonTest version
#ifdef EON_VERSION
    snap.eonVersion = QStringLiteral(EON_VERSION);
#else
    snap.eonVersion = "unknown";
#endif

    // 扫描插件目录收集版本
    QStringList pluginSearchPaths = QCoreApplication::libraryPaths();
    for (const auto& path : pluginSearchPaths) {
        QDir dir(path);
        if (!dir.exists()) continue;
        for (const auto& fileInfo : dir.entryInfoList({"*.dll", "*.so", "*.dylib"}, QDir::Files)) {
            QPluginLoader loader(fileInfo.absoluteFilePath());
            QJsonObject meta = loader.metaData().value("MetaData").toObject();
            QString pluginId = meta.value("pluginId").toString();
            QString pluginVer = meta.value("version").toString();
            if (!pluginId.isEmpty()) {
                snap.pluginVersions.append(QString("%1==%2").arg(pluginId, pluginVer));
            }
            loader.unload();
        }
    }

    return snap;
}

bool writeEnvSnapshot(const QString& filePath, const EnvSnapshot& snapshot)
{
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QJsonDocument doc(snapshot.toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

} // namespace eon::runtime
