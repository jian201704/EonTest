#include "eon/sdk/ArtifactPipeline.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace eon::sdk {

void ArtifactPipeline::addListener(IArtifactListener* listener) {
    if (listener && !listeners_.contains(listener))
        listeners_.append(listener);
}

void ArtifactPipeline::removeListener(IArtifactListener* listener) {
    listeners_.removeAll(listener);
}

void ArtifactPipeline::publishArtifact(const QString& filePath, const QString& mimeType) {
    // 记录路径
    if (!publishedPaths_.contains(filePath))
        publishedPaths_.append(filePath);

    // 通知所有监听器
    for (auto* listener : listeners_) {
        // 默认 sourceStepId 为空，由引擎在外层设置
        listener->onArtifactPublished(filePath, mimeType, {});
    }
}

void ArtifactPipeline::publishArtifact(QIODevice* stream, const QString& fileName) {
    if (!stream) return;

    // 将流写入临时文件
    QString tempPath = reportsDir_ + "/" + cellId_ + "/artifacts/" + fileName;
    QDir().mkpath(QFileInfo(tempPath).absolutePath());
    QFile file(tempPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(stream->readAll());
        file.close();
        publishArtifact(tempPath, "application/octet-stream");
    }
}

void ArtifactPipeline::clear() {
    publishedPaths_.clear();
}

} // namespace eon::sdk
