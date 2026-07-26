#pragma once

#include <QIODevice>
#include <QString>

namespace eon::sdk {

/// <summary>
/// Artifact 发布者接口（对标 OpenTAP TestStepRun.PublishArtifact）。
/// 步骤/报告器通过此接口发布文件或流 artifact。
/// </summary>
class IArtifactPublisher {
public:
    virtual ~IArtifactPublisher() = default;

    /// 发布文件路径 artifact（如截图 PNG、CSV 数据文件）
    virtual void publishArtifact(const QString& filePath,
                                  const QString& mimeType = {}) = 0;

    /// 发布流 artifact（内存数据，引擎负责写入磁盘或转发）
    virtual void publishArtifact(QIODevice* stream,
                                  const QString& fileName) = 0;
};

/// <summary>
/// Artifact 监听器接口（对标 OpenTAP IArtifactListener）。
/// 结果收集器实现此接口，接收所有 artifact 通知。
/// 支持链式处理：Listener A 产出 Artifact → Listener B 再处理。
/// </summary>
class IArtifactListener {
public:
    virtual ~IArtifactListener() = default;

    /// 收到 artifact 通知
    /// @param artifactPath artifact 文件路径或逻辑名称
    /// @param mimeType MIME 类型（如 "text/csv", "image/png"）
    /// @param sourceStepId 来源步骤 ID
    virtual void onArtifactPublished(const QString& artifactPath,
                                      const QString& mimeType,
                                      const QString& sourceStepId) = 0;
};

} // namespace eon::sdk
