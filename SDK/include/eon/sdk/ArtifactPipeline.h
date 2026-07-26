#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>

#include "eon/sdk/IArtifactListener.h"
#include "eon/sdk/StepResult.h"

namespace eon::sdk {

/// <summary>
/// Artifact 管道中介（对标 OpenTAP TestPlanRun.PublishArtifact 机制）。
/// 步骤通过 IArtifactPublisher 发布 artifact，
/// WorkflowEngine 作为中介收集并转发给所有 IArtifactListener。
///
/// 支持链式处理：Listener A 产出 Artifact → 自动通知 Listener B。
/// </summary>
class ArtifactPipeline : public IArtifactPublisher {
public:
    ArtifactPipeline() = default;

    /// 注册 artifact 监听器
    void addListener(IArtifactListener* listener);

    /// 移除 artifact 监听器
    void removeListener(IArtifactListener* listener);

    // ── IArtifactPublisher ────────────────────────────────────
    void publishArtifact(const QString& filePath,
                         const QString& mimeType = {}) override;

    void publishArtifact(QIODevice* stream,
                         const QString& fileName) override;

    /// 获取所有已发布的 artifact 路径列表
    QStringList publishedArtifacts() const { return publishedPaths_; }

    /// 清空已发布列表（每次 workflow 执行前调用）
    void clear();

private:
    QVector<IArtifactListener*> listeners_;
    QStringList publishedPaths_;
    QString reportsDir_;
    QString cellId_;
};

} // namespace eon::sdk
