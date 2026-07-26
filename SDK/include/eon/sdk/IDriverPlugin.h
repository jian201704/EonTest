#pragma once

#include <QString>
#include <QVariantMap>
#include <QtPlugin>

namespace eon::sdk {

// ============================================================================
// BusType — 总线类型枚举
// ============================================================================
enum class BusType {
    Serial,      // RS232/RS485/TTL 串口
    CAN,         // CAN 2.0A/B
    CANFD,       // CAN FD (ISO 11898-1:2015)
    LIN,         // LIN 总线
    USB,         // USB HID / Bulk / CDC
    I2C,         // I2C 总线（经 USB 适配器）
    SPI,         // SPI 总线（经 USB 适配器）
    GPIB,        // IEEE 488
    TCP,         // TCP/IP 网络
    UDP,         // UDP 网络
    Virtual      // 虚拟总线（调试/回放）
};

inline QString busTypeToString(BusType type) {
    switch (type) {
    case BusType::Serial:  return "serial";
    case BusType::CAN:     return "can";
    case BusType::CANFD:   return "canfd";
    case BusType::LIN:     return "lin";
    case BusType::USB:     return "usb";
    case BusType::I2C:     return "i2c";
    case BusType::SPI:     return "spi";
    case BusType::GPIB:    return "gpib";
    case BusType::TCP:     return "tcp";
    case BusType::UDP:     return "udp";
    case BusType::Virtual: return "virtual";
    }
    return "unknown";
}

inline BusType busTypeFromString(const QString& text) {
    const QString lower = text.toLower().trimmed();
    if (lower == "serial" || lower == "rs232" || lower == "com")  return BusType::Serial;
    if (lower == "can")     return BusType::CAN;
    if (lower == "canfd")   return BusType::CANFD;
    if (lower == "lin")     return BusType::LIN;
    if (lower == "usb")     return BusType::USB;
    if (lower == "i2c")     return BusType::I2C;
    if (lower == "spi")     return BusType::SPI;
    if (lower == "gpib")    return BusType::GPIB;
    if (lower == "tcp")     return BusType::TCP;
    if (lower == "udp")     return BusType::UDP;
    if (lower == "virtual") return BusType::Virtual;
    return BusType::Virtual;
}

// ============================================================================
// BusConfig — 总线配置基类
// ============================================================================
struct BusConfig {
    BusType type = BusType::Virtual;
    QString busId;          // 总线唯一标识，如 "CAN0", "COM3"
    QVariantMap properties; // 总线特定参数

    virtual ~BusConfig() = default;
};

// ============================================================================
// SerialConfig — 串口配置
// ============================================================================
struct SerialConfig : BusConfig {
    QString portName;         // "COM3" / "/dev/ttyUSB0"
    int baudRate = 115200;
    int dataBits = 8;
    int stopBits = 1;
    QString parity = "none";  // none / odd / even
    QString flowControl = "none"; // none / rtscts / xonxoff

    SerialConfig() { type = BusType::Serial; }
};

// ============================================================================
// CanConfig — CAN 总线配置
// ============================================================================
struct CanConfig : BusConfig {
    QString interfaceName;   // "can0" / "PCAN_USBBUS1"
    int bitrate = 500000;    // 500 kbps (CAN)
    int dataBitrate = 2000000; // 2 Mbps (CAN FD 数据段)
    bool fdEnabled = false;  // CAN FD 使能
    bool listenOnly = false;

    CanConfig() { type = BusType::CAN; }
};

// ============================================================================
// BusFrame — 总线数据帧
// ============================================================================
struct BusFrame {
    quint32 id = 0;           // CAN ID / 地址
    QByteArray data;          // 负载数据
    quint64 timestampUs = 0;  // 微秒时间戳
    bool isExtended = false;  // CAN 扩展帧
    bool isRemote = false;    // CAN 远程帧
    bool isError = false;     // 错误帧
    bool isFd = false;        // CAN FD 帧
    quint8 channel = 0;       // 多通道设备
};

// ============================================================================
// BusDriverState — 总线驱动状态
// ============================================================================
enum class BusDriverState {
    Disconnected,
    Connecting,
    Connected,
    Error
};

// ============================================================================
// IDriverPlugin — 驱动插件基础契约
// 所有硬件驱动插件必须实现此接口
// ============================================================================
class IDriverPlugin {
public:
    virtual ~IDriverPlugin() = default;

    /// 插件唯一标识，如 "eon.driver.serial", "eon.driver.socketcan"
    virtual QString id() const = 0;

    /// 插件显示名称
    virtual QString displayName() const = 0;

    /// 插件版本号
    virtual QString version() const = 0;

    /// 支持的驱动能力声明
    virtual QVariantMap capabilities() const = 0;

    /// 初始化驱动（分配资源但不连接硬件）
    virtual bool initialize(QString& errorMessage) = 0;

    /// 关闭驱动并释放资源
    virtual void shutdown() = 0;

    /// 是否已初始化
    virtual bool isInitialized() const = 0;
};

// ============================================================================
// IBusDriver — 总线驱动契约
// 用于 CAN/串口/USB 等总线通信
// ============================================================================
class IBusDriver : public IDriverPlugin {
public:
    ~IBusDriver() override = default;

    /// 打开总线连接
    virtual bool open(const BusConfig& config, QString& errorMessage) = 0;

    /// 关闭总线连接
    virtual void close() = 0;

    /// 是否已连接
    virtual bool isOpen() const = 0;

    /// 获取当前总线状态
    virtual BusDriverState state() const = 0;

    /// 获取支持的总线类型
    virtual BusType busType() const = 0;

    /// 发送数据帧
    virtual bool send(const BusFrame& frame, QString& errorMessage) = 0;

    /// 接收数据帧（阻塞，带超时 ms，0=立即返回）
    virtual bool receive(BusFrame& frame, int timeoutMs, QString& errorMessage) = 0;

    /// 清空接收缓冲区
    virtual void flushReceiveBuffer() = 0;

    /// 清空发送缓冲区
    virtual void flushSendBuffer() = 0;

    /// 获取底层错误计数（CAN 用）
    virtual QVariantMap errorCounters() const = 0;
};

// ============================================================================
// IProtocolLayer — 协议层契约
// 在总线驱动之上实现诊断/标定协议
// ============================================================================
class IProtocolLayer : public IDriverPlugin {
public:
    ~IProtocolLayer() override = default;

    /// 绑定底层总线驱动
    virtual bool bindBus(IBusDriver* busDriver, QString& errorMessage) = 0;

    /// 解绑总线驱动
    virtual void unbindBus() = 0;

    /// 协议层是否已就绪（已绑定总线
    virtual bool isReady() const = 0;

    /// 发送协议请求
    virtual bool sendRequest(const QByteArray& request, QByteArray& response,
                             int timeoutMs, QString& errorMessage) = 0;

    /// 发送协议请求（带帧 ID 过滤，用于 CAN 多节点场景）
    virtual bool sendRequestEx(const BusFrame& requestFrame, BusFrame& responseFrame,
                               int timeoutMs, QString& errorMessage) = 0;

    /// 获取协议名称
    virtual QString protocolName() const = 0;

    /// 获取协议版本
    virtual QString protocolVersion() const = 0;
};

// ============================================================================
// IDeviceDriver — 设备驱动契约
// 用于特定测试设备（电源、万用表、示波器等）
// ============================================================================
class IDeviceDriver : public IDriverPlugin {
public:
    ~IDeviceDriver() override = default;

    /// 连接设备
    virtual bool connect(const QVariantMap& config, QString& errorMessage) = 0;

    /// 断开设备
    virtual void disconnect() = 0;

    /// 是否已连接
    virtual bool isConnected() const = 0;

    /// 设备自检
    virtual bool selfTest(QString& errorMessage) = 0;

    /// 发送命令
    virtual bool sendCommand(const QString& command, const QVariantMap& params,
                             QVariantMap& result, QString& errorMessage) = 0;

    /// 读取测量值
    virtual bool readMeasurement(const QString& channel, double& value,
                                 QString& unit, QString& errorMessage) = 0;

    /// 设备信息
    virtual QVariantMap deviceInfo() const = 0;
};

} // namespace eon::sdk

// ============================================================================
// Qt Plugin IID 声明
// ============================================================================
#define EON_IDRIVERPLUGIN_IID "com.eontest.sdk.IDriverPlugin/1.0"
#define EON_IBUSDRIVER_IID    "com.eontest.sdk.IBusDriver/1.0"
#define EON_IPROTOCOLLAYER_IID "com.eontest.sdk.IProtocolLayer/1.0"
#define EON_IDEVICEDRIVER_IID "com.eontest.sdk.IDeviceDriver/1.0"

Q_DECLARE_INTERFACE(eon::sdk::IDriverPlugin,   EON_IDRIVERPLUGIN_IID)
Q_DECLARE_INTERFACE(eon::sdk::IBusDriver,      EON_IBUSDRIVER_IID)
Q_DECLARE_INTERFACE(eon::sdk::IProtocolLayer,   EON_IPROTOCOLLAYER_IID)
Q_DECLARE_INTERFACE(eon::sdk::IDeviceDriver,   EON_IDEVICEDRIVER_IID)
