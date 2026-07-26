#include "eon/infra/VirtualBusDriver.h"

#include <QDateTime>
#include <QThread>

namespace eon::infra {

VirtualBusDriver::VirtualBusDriver(QObject* parent)
    : QObject(parent) {}

VirtualBusDriver::~VirtualBusDriver() {
    shutdown();
}

QVariantMap VirtualBusDriver::capabilities() const {
    return {
        {"busType", "virtual"},
        {"supportsLoopback", true},
        {"supportsReplay", true},
        {"supportsRecording", true},
        {"maxFrameSize", 4096},
        {"maxPendingFrames", 10000}
    };
}

bool VirtualBusDriver::initialize(QString& errorMessage) {
    Q_UNUSED(errorMessage)
    QMutexLocker lock(&mutex_);
    if (initialized_) return true;
    initialized_ = true;
    return true;
}

void VirtualBusDriver::shutdown() {
    QMutexLocker lock(&mutex_);
    close();
    initialized_ = false;
    replayFrames_.clear();
    recordedFrames_.clear();
    receiveQueue_.clear();
    sendQueue_.clear();
    replayIndex_ = 0;
}

bool VirtualBusDriver::open(const eon::sdk::BusConfig& config, QString& errorMessage) {
    Q_UNUSED(errorMessage)
    QMutexLocker lock(&mutex_);
    if (!initialized_) {
        if (!errorMessage.isEmpty()) errorMessage = "VirtualBus not initialized.";
        return false;
    }
    if (opened_) return true;

    loopback_ = config.properties.value("loopback", true).toBool();
    recording_ = config.properties.value("recording", false).toBool();
    frameDelayUs_ = config.properties.value("frameDelayUs", 0).toLongLong();

    opened_ = true;
    return true;
}

void VirtualBusDriver::close() {
    QMutexLocker lock(&mutex_);
    opened_ = false;
    receiveQueue_.clear();
    sendQueue_.clear();
    replayIndex_ = 0;
    frameAvailable_.wakeAll();
}

eon::sdk::BusDriverState VirtualBusDriver::state() const {
    QMutexLocker lock(&mutex_);
    if (!initialized_) return eon::sdk::BusDriverState::Disconnected;
    if (!opened_) return eon::sdk::BusDriverState::Disconnected;
    return eon::sdk::BusDriverState::Connected;
}

bool VirtualBusDriver::send(const eon::sdk::BusFrame& frame, QString& errorMessage) {
    QMutexLocker lock(&mutex_);
    if (!opened_) {
        errorMessage = "VirtualBus is not open.";
        return false;
    }

    eon::sdk::BusFrame stamped = frame;
    stamped.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;

    if (recording_) {
        recordedFrames_.append(stamped);
    }

    sendQueue_.append(stamped);

    // 软件回环：发送的帧同时进入接收队列
    if (loopback_) {
        receiveQueue_.append(stamped);
        frameAvailable_.wakeOne();
    }

    return true;
}

bool VirtualBusDriver::receive(eon::sdk::BusFrame& frame, int timeoutMs, QString& errorMessage) {
    QMutexLocker lock(&mutex_);
    if (!opened_) {
        errorMessage = "VirtualBus is not open.";
        return false;
    }

    // 优先回放模式
    if (!replayFrames_.isEmpty() && replayIndex_ < replayFrames_.size()) {
        if (frameDelayUs_ > 0) {
            // 释放锁等待模拟延迟
            lock.unlock();
            QThread::usleep(static_cast<unsigned long>(frameDelayUs_));
            lock.relock();
        }
        frame = replayFrames_.at(replayIndex_++);
        return true;
    }

    // 从接收队列取帧
    if (timeoutMs == 0) {
        // 非阻塞
        if (receiveQueue_.isEmpty()) {
            errorMessage = "No frame available.";
            return false;
        }
        frame = receiveQueue_.takeFirst();
        return true;
    }

    if (timeoutMs < 0) {
        // 无限等待
        while (receiveQueue_.isEmpty() && opened_) {
            frameAvailable_.wait(&mutex_);
        }
    } else {
        // 带超时等待
        if (receiveQueue_.isEmpty()) {
            frameAvailable_.wait(&mutex_, static_cast<unsigned long>(timeoutMs));
        }
    }

    if (receiveQueue_.isEmpty()) {
        errorMessage = "Receive timeout.";
        return false;
    }

    frame = receiveQueue_.takeFirst();
    return true;
}

void VirtualBusDriver::flushReceiveBuffer() {
    QMutexLocker lock(&mutex_);
    receiveQueue_.clear();
}

void VirtualBusDriver::flushSendBuffer() {
    QMutexLocker lock(&mutex_);
    sendQueue_.clear();
}

QVariantMap VirtualBusDriver::errorCounters() const {
    QMutexLocker lock(&mutex_);
    return {
        {"txCount", sendQueue_.size()},
        {"rxCount", receiveQueue_.size()},
        {"replayIndex", replayIndex_},
        {"replayTotal", replayFrames_.size()}
    };
}

// --- VirtualBus 特有方法 ---

void VirtualBusDriver::setReplayFrames(const QList<eon::sdk::BusFrame>& frames) {
    QMutexLocker lock(&mutex_);
    replayFrames_ = frames;
    replayIndex_ = 0;
}

QList<eon::sdk::BusFrame> VirtualBusDriver::recordedFrames() const {
    QMutexLocker lock(&mutex_);
    return recordedFrames_;
}

void VirtualBusDriver::clearRecordedFrames() {
    QMutexLocker lock(&mutex_);
    recordedFrames_.clear();
}

int VirtualBusDriver::pendingFrameCount() const {
    QMutexLocker lock(&mutex_);
    return receiveQueue_.size();
}

void VirtualBusDriver::injectFrame(const eon::sdk::BusFrame& frame) {
    QMutexLocker lock(&mutex_);
    eon::sdk::BusFrame stamped = frame;
    if (stamped.timestampUs == 0) {
        stamped.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    }
    receiveQueue_.append(stamped);
    frameAvailable_.wakeOne();
}

void VirtualBusDriver::setFrameDelayUs(qint64 delayUs) {
    QMutexLocker lock(&mutex_);
    frameDelayUs_ = delayUs;
}

} // namespace eon::infra

#include "VirtualBusDriver.moc"
