#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

#include "eon/sdk/ITransport.h"

namespace eon::sdk {

/// <summary>
/// SCPI I/O 抽象接口。
/// 统一串口/GPIB/TCPIP 等底层通信，上层仪器驱动不关心具体传输方式。
///
/// 可通过 transport() 获取底层 ITransport 接口，用于非 SCPI 协议的字节级操作。
/// </summary>
class IScpiIO {
public:
    virtual ~IScpiIO() = default;

    /// <summary>
    /// 打开通信通道。
    /// </summary>
    virtual bool open(const QVariantMap& config) = 0;

    /// <summary>
    /// 关闭通信通道。
    /// </summary>
    virtual void close() = 0;

    /// <summary>
    /// 是否已连接。
    /// </summary>
    virtual bool isConnected() const = 0;

    /// <summary>
    /// 清除 SCPI 状态（包括错误队列）。对应 *CLS。
    /// </summary>
    virtual bool deviceClear() = 0;

    /// <summary>
    /// 发送 SCPI 命令（不读取响应）。
    /// </summary>
    /// <param name="cmd">完整的 SCPI 命令。</param>
    /// <param name="timeoutMs">写入超时 ms。</param>
    /// <returns>true 表示发送成功。</returns>
    virtual bool writeCommand(const QString& cmd, int timeoutMs = 200) = 0;

    /// <summary>
    /// 发送 SCPI 查询并读取一行响应。
    /// </summary>
    /// <param name="query">查询命令（如 *IDN?）。</param>
    /// <param name="timeoutMs">读取超时 ms。</param>
    /// <returns>响应字符串（不含终止符）。</returns>
    virtual QString query(const QString& query, int timeoutMs = 1000) = 0;

    /// <summary>
    /// 读取错误队列。对应 SYST:ERR?。
    /// </summary>
    /// <returns>错误描述字符串，空字符串表示无错误。</returns>
    virtual QString readError() = 0;

    /// <summary>
    /// 返回当前配置信息（用于日志和调试）。
    /// </summary>
    virtual QString configInfo() const = 0;

    /// <summary>
    /// 获取底层 ITransport 接口（可选覆盖）。
    /// 非 SCPI 协议可通过此接口进行原始字节读写。
    /// 默认返回 nullptr，表示不支持或无需底层传输访问。
    /// </summary>
    virtual ITransport* transport() { return nullptr; }
    virtual const ITransport* transport() const { return nullptr; }
};

} // namespace eon::sdk
