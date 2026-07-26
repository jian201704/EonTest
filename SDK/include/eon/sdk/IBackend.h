#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace eon::sdk {

// ============================================================================
// IBackend — 仪器通信后端抽象接口
//
// 三种实现：
//   SerialBackend   — Qt QSerialPort 串口
//   PythonProcessBackend — QProcess + Python JSON IPC
//   EmbeddedPythonBackend — libpython 内嵌
//
// 使用方式（工厂模式）：
//   auto backend = BackendFactory::create("serial", {{"port", "COM5"}});
//   backend->open(config, error);
//   backend->write("*IDN?\n", error);
//   QString idn = backend->readUntil("\n", 500, error);
// ============================================================================
class IBackend {
public:
    virtual ~IBackend() = default;

    /// 打开连接，config 包含后端特定参数（端口号、波特率、脚本路径等）
    virtual bool open(const QVariantMap& config, QString& errorMessage) = 0;

    /// 关闭连接
    virtual void close() = 0;

    /// 是否已连接
    virtual bool isOpen() const = 0;

    /// 写入数据（同步，阻塞至写完或超时）
    virtual bool write(const QByteArray& data, int timeoutMs, QString& errorMessage) = 0;

    /// 读取直到遇到 terminator（如 "\n"），或总超时到期
    virtual QByteArray readUntil(const QByteArray& terminator, int timeoutMs, QString& errorMessage) = 0;

    /// 读取一行（等价于 readUntil("\n", timeoutMs, error)）
    virtual QByteArray readLine(int timeoutMs, QString& errorMessage) = 0;

    /// 清空接收缓冲区
    virtual void flush() = 0;

    /// 后端类型名，用于日志和调试
    virtual QString typeName() const = 0;
};

} // namespace eon::sdk
