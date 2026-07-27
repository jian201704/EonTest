#include "eon/infra/SocketCanDriver.h"

#include <QDateTime>
#include <QThread>

// Linux SocketCAN requires platform-specific headers
// On non-Linux platforms, all methods return errors with appropriate messages.
#if defined(Q_OS_LINUX)
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <cstring>
#endif

namespace eon::infra {

SocketCanDriver::SocketCanDriver(QObject* parent)
    : QObject(parent) {}

SocketCanDriver::~SocketCanDriver() {
    shutdown();
}

QVariantMap SocketCanDriver::capabilities() const {
    return {
        {"busType", "can"},
        {"supportsCanFd", true},
        {"supportsListenOnly", true},
        {"supportsErrorFrames", true},
        {"platform", "linux"}
    };
}

bool SocketCanDriver::initialize(QString& errorMessage) {
#if !defined(Q_OS_LINUX)
    errorMessage = "SocketCAN is only available on Linux.";
    return false;
#else
    if (initialized_) return true;
    initialized_ = true;
    return true;
#endif
}

void SocketCanDriver::shutdown() {
    close();
    initialized_ = false;
}

bool SocketCanDriver::open(const eon::sdk::BusConfig& config, QString& errorMessage) {
#if !defined(Q_OS_LINUX)
    Q_UNUSED(config)
    errorMessage = "SocketCAN is only available on Linux.";
    return false;
#else
    if (!initialized_) {
        errorMessage = "SocketCAN driver not initialized.";
        return false;
    }

    ifName_ = config.properties.value("interfaceName", config.busId).toString();

    // 创建 CAN_RAW socket
    int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        errorMessage = QString("socket(PF_CAN) failed: %1").arg(strerror(errno));
        return false;
    }

    // 获取接口索引
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, ifName_.toLocal8Bit().constData(), IFNAMSIZ - 1);

    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        errorMessage = QString("ioctl(SIOCGIFINDEX) for '%1' failed: %2").arg(ifName_, strerror(errno));
        ::close(fd);
        return false;
    }

    // 绑定 socket 到接口
    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        errorMessage = QString("bind() to '%1' failed: %2").arg(ifName_, strerror(errno));
        ::close(fd);
        return false;
    }

    // CAN FD 支持
    const bool fdEnabled = config.properties.value("fdEnabled", false).toBool();
    if (fdEnabled) {
        int enable = 1;
        if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable)) < 0) {
            qWarning() << "SocketCAN: CAN FD not supported on this kernel, falling back to CAN 2.0";
        }
    }

    // 回环
    const int loopback = config.properties.value("loopback", true).toBool() ? 1 : 0;
    ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &loopback, sizeof(loopback));

    // 错误帧接收
    const int recvErrors = config.properties.value("recvErrors", false).toBool() ? 1 : 0;
    ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &recvErrors, sizeof(recvErrors));

    socketFd_ = fd;
    return true;
#endif
}

void SocketCanDriver::close() {
#if defined(Q_OS_LINUX)
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
#endif
}

bool SocketCanDriver::isOpen() const {
#if defined(Q_OS_LINUX)
    return socketFd_ >= 0;
#else
    return false;
#endif
}

eon::sdk::BusDriverState SocketCanDriver::state() const {
    if (!initialized_) return eon::sdk::BusDriverState::Disconnected;
    return isOpen() ? eon::sdk::BusDriverState::Connected : eon::sdk::BusDriverState::Disconnected;
}

eon::sdk::BusType SocketCanDriver::busType() const {
    return eon::sdk::BusType::CAN;
}

bool SocketCanDriver::send(const eon::sdk::BusFrame& frame, QString& errorMessage) {
#if !defined(Q_OS_LINUX)
    Q_UNUSED(frame)
    errorMessage = "SocketCAN is only available on Linux.";
    return false;
#else
    if (socketFd_ < 0) {
        errorMessage = "SocketCAN not open.";
        return false;
    }

    struct can_frame canFrame;
    std::memset(&canFrame, 0, sizeof(canFrame));

    if (frame.isFd) {
        errorMessage = "CAN FD sending not yet implemented.";
        return false;
    }

    // CAN ID
    canFrame.can_id = frame.id;
    if (frame.isExtended) {
        canFrame.can_id |= CAN_EFF_FLAG;
    }
    if (frame.isRemote) {
        canFrame.can_id |= CAN_RTR_FLAG;
    }

    // 数据长度
    canFrame.can_dlc = static_cast<quint8>(qMin(frame.data.size(), 8));

    // 拷贝数据
    std::memcpy(canFrame.data, frame.data.constData(),
                static_cast<size_t>(canFrame.can_dlc));

    const int written = static_cast<int>(
        ::write(socketFd_, &canFrame, CAN_MTU));
    if (written < 0) {
        errorMessage = QString("SocketCAN send failed: %1").arg(strerror(errno));
        return false;
    }
    if (written != CAN_MTU) {
        errorMessage = QString("SocketCAN partial send: %1/%2").arg(written).arg(CAN_MTU);
        return false;
    }

    return true;
#endif
}

bool SocketCanDriver::receive(eon::sdk::BusFrame& frame, int timeoutMs, QString& errorMessage) {
#if !defined(Q_OS_LINUX)
    Q_UNUSED(frame) Q_UNUSED(timeoutMs)
    errorMessage = "SocketCAN is only available on Linux.";
    return false;
#else
    if (socketFd_ < 0) {
        errorMessage = "SocketCAN not open.";
        return false;
    }

    // select() 等待数据
    if (timeoutMs != 0) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketFd_, &readSet);

        struct timeval tv;
        struct timeval* tvPtr = nullptr;
        if (timeoutMs > 0) {
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            tvPtr = &tv;
        }

        const int ret = ::select(socketFd_ + 1, &readSet, nullptr, nullptr, tvPtr);
        if (ret < 0) {
            errorMessage = QString("SocketCAN select() failed: %1").arg(strerror(errno));
            return false;
        }
        if (ret == 0) {
            errorMessage = "SocketCAN receive timeout.";
            return false;
        }
    }

    struct can_frame canFrame;
    std::memset(&canFrame, 0, sizeof(canFrame));

    const int nbytes = static_cast<int>(::read(socketFd_, &canFrame, sizeof(canFrame)));
    if (nbytes < 0) {
        errorMessage = QString("SocketCAN read() failed: %1").arg(strerror(errno));
        return false;
    }
    if (nbytes < static_cast<int>(sizeof(struct can_frame))) {
        errorMessage = QString("SocketCAN incomplete frame: %1 bytes").arg(nbytes);
        return false;
    }

    // 转换帧
    frame.id = canFrame.can_id & CAN_EFF_MASK;
    frame.isExtended = (canFrame.can_id & CAN_EFF_FLAG) != 0;
    frame.isRemote = (canFrame.can_id & CAN_RTR_FLAG) != 0;
    frame.isError = (canFrame.can_id & CAN_ERR_FLAG) != 0;
    frame.isFd = false;

    const int dlc = canFrame.can_dlc;
    frame.data = QByteArray(reinterpret_cast<const char*>(canFrame.data), dlc);
    frame.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;

    return true;
#endif
}

void SocketCanDriver::flushReceiveBuffer() {
#if defined(Q_OS_LINUX)
    // 清空 socket 缓冲区
    if (socketFd_ >= 0) {
        struct can_frame dummy;
        while (::recv(socketFd_, &dummy, sizeof(dummy), MSG_DONTWAIT) > 0) {
            // 丢弃
        }
    }
#endif
}

void SocketCanDriver::flushSendBuffer() {
    // SocketCAN 无发送缓冲区概念
}

QVariantMap SocketCanDriver::errorCounters() const {
#if defined(Q_OS_LINUX)
    return {
        {"interface", ifName_},
        {"fd", socketFd_}
    };
#else
    return {{"platform", "not linux"}};
#endif
}

} // namespace eon::infra

