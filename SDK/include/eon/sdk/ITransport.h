#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace eon::sdk {

/// <summary>
/// 通用传输层抽象接口（参考 OpenTAP IVisa / IScpiIO 设计扩展）。
///
/// 用途：
/// - SCPI 仪器：通过 IScpiIO（继承自 ITransport）使用
/// - 非 SCPI 仪器（Modbus、REST API、二进制协议、厂商 DLL）：直接实现 ITransport
///
/// 职责：
/// - 打开/关闭物理连接（串口、TCP、VISA、文件、管道等）
/// - 原始字节读写
/// - 超时控制
///
/// 与 IScpiIO 的关系：
///   ITransport（字节级）
///       ↑
///   IScpiIO（SCPI 语义级：*CLS, *ESR?, SYST:ERR?）
///
/// 示例实现：
///   SerialTransport, TcpTransport, VisaTransport（通过 IScpiIO）
///   ModbusTransport, HttpTransport（直接 ITransport）
/// </summary>
class ITransport {
public:
    virtual ~ITransport() = default;

    /// <summary>
    /// 打开传输通道。
    /// </summary>
    /// <param name="config">连接参数（port/host/baudRate/connectType 等）</param>
    /// <returns>true 表示连接成功。</returns>
    virtual bool open(const QVariantMap& config) = 0;

    /// <summary>
    /// 关闭传输通道。
    /// </summary>
    virtual void close() = 0;

    /// <summary>
    /// 是否已连接。
    /// </summary>
    virtual bool isConnected() const = 0;

    /// <summary>
    /// 读取原始字节。
    /// </summary>
    /// <param name="timeoutMs">超时毫秒。</param>
    /// <returns>读取到的字节，为空表示超时或失败。</returns>
    virtual QByteArray readBytes(int timeoutMs) = 0;

    /// <summary>
    /// 写入原始字节。
    /// </summary>
    /// <param name="data">待写入数据。</param>
    /// <param name="timeoutMs">超时毫秒。</param>
    /// <returns>true 表示写入成功。</returns>
    virtual bool writeBytes(const QByteArray& data, int timeoutMs) = 0;

    /// <summary>
    /// 返回传输层描述信息（用于日志和调试）。
    /// </summary>
    virtual QString transportName() const = 0;

    /// <summary>
    /// 清除传输缓冲区（可选覆盖）。
    /// 默认实现为空操作。
    /// </summary>
    virtual void flush() {}
};

} // namespace eon::sdk
