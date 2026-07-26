#pragma once

#include <eon/sdk/IBackend.h>
#include <QProcess>
#include <QJsonObject>

namespace eon::infra {

// ============================================================================
// PythonProcessBackend — QProcess + Python JSON IPC 后端
//
// config 参数：
//   command       - Python 解释器路径，默认 "python3"
//   script        - Python 脚本路径（必填）
//   workDir       - 工作目录（可选）
//   timeoutMs     - 单次 IPC 调用超时，默认 10000
//
// 协议：每行一个 JSON 对象
//   request:  {"cmd": "...", "params": {...}, "id": N}
//   response: {"id": N, "result": {...}, "error": ""}
// ============================================================================
class PythonProcessBackend final : public eon::sdk::IBackend {
public:
    PythonProcessBackend();
    ~PythonProcessBackend() override;

    bool open(const QVariantMap& config, QString& errorMessage) override;
    void close() override;
    bool isOpen() const override;
    bool write(const QByteArray& data, int timeoutMs, QString& errorMessage) override;
    QByteArray readUntil(const QByteArray& terminator, int timeoutMs, QString& errorMessage) override;
    QByteArray readLine(int timeoutMs, QString& errorMessage) override;
    void flush() override;
    QString typeName() const override { return "python_process"; }

    /// Python 专用：发送 JSON 请求，等待 JSON 响应
    QJsonObject sendRequest(const QJsonObject& request, int timeoutMs, QString& errorMessage);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eon::infra
