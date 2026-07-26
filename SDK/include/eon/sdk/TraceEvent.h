#pragma once

#include <QJsonObject>
#include <QString>

namespace eon::sdk {

/// <summary>
/// SCPI 跟踪事件（JSON Lines 格式，每行一个 JSON 对象）。
/// 对应 OpenTAP 的 scpi.trace + 结构化事件。
/// </summary>
struct ScpiTraceEvent {
    QString timestamp;      // ISO8601 UTC
    QString cellId;         // cell-01
    QString stepId;         // workflow.step-12
    QString dutId;          // DUT 序列号
    QString resourceId;     // VISA::TCPIP::10.0.0.1::5025
    QString dir;            // "tx" | "rx"
    QString payload;        // 命令/响应内容
    qint64 durationMs = 0;  // 耗时
    QString status;         // "ok" | "timeout" | "error"
    QString error;          // 错误描述
    int threadId = 0;       // 线程 ID
    int leaseId = -1;       // 资源租约 ID

    /// 转为 JSON 对象（用于写入 JSON Lines 文件）
    QJsonObject toJson() const {
        return {
            {"ts", timestamp},
            {"cellId", cellId},
            {"stepId", stepId},
            {"dutId", dutId},
            {"resourceId", resourceId},
            {"dir", dir},
            {"payload", payload},
            {"durationMs", durationMs},
            {"status", status},
            {"error", error},
            {"threadId", threadId},
            {"leaseId", leaseId}
        };
    }

    /// 从 JSON 对象解析
    static ScpiTraceEvent fromJson(const QJsonObject& obj) {
        return {
            .timestamp = obj.value("ts").toString(),
            .cellId = obj.value("cellId").toString(),
            .stepId = obj.value("stepId").toString(),
            .dutId = obj.value("dutId").toString(),
            .resourceId = obj.value("resourceId").toString(),
            .dir = obj.value("dir").toString(),
            .payload = obj.value("payload").toString(),
            .durationMs = obj.value("durationMs").toInteger(),
            .status = obj.value("status").toString(),
            .error = obj.value("error").toString(),
            .threadId = obj.value("threadId").toInt(),
            .leaseId = obj.value("leaseId").toInt(-1)
        };
    }
};

} // namespace eon::sdk
