#include "eon/sdk/ScpiInstrument.h"
#include <QDebug>
#include <QElapsedTimer>

namespace eon::sdk {

ScpiInstrument::ScpiInstrument()
    : QObject(nullptr)
{
    // OpenTAP 默认值：SendClearOnConnect=true, SendIDNOnConnect=true, SendCLSOnConnect=true
    sendClearOnConnect_ = true;
    sendIDNOnConnect_ = true;
    sendCLSOnConnect_ = true;
    queryErrorAfterCommand_ = false;
    verboseLogging_ = true;
    ioTimeoutMs_ = 2000;
}

ScpiInstrument::~ScpiInstrument() {
    close();
}

// ============================================================
// IResource: open() — 参考 OpenTAP ScpiInstrument.Open()
// 初始化序列：
//   1. io_->open(config) — 打开底层传输
//   2. 若 sendClearOnConnect: io_->deviceClear() — VIClear
//   3. 调用 base::Open() — 标记已连接
//   4. 若 sendIDNOnConnect: queryIdn() — *IDN?
//   5. 若 sendCLSOnConnect: commandCls() — *CLS
// ============================================================
bool ScpiInstrument::open() {
    if (!io_) {
        qWarning() << "ScpiInstrument: io_ is null, call setIo() before open()";
        return false;
    }

    QVariantMap config;
    config.insert("host", visaAddress_);
    config.insert("timeoutMs", ioTimeoutMs_);

    // 1. 打开底层传输
    if (!io_->open(config)) {
        qWarning() << "ScpiInstrument: failed to open IO:" << visaAddress_;
        return false;
    }
    qDebug().noquote() << QString("ScpiInstrument: IO opened [%1]").arg(io_->configInfo());

    // 2. VIClear（对应 OpenTAP SendClearOnConnect）
    if (sendClearOnConnect_) {
        io_->deviceClear();
        qDebug() << "ScpiInstrument: VIClear sent";
    }

    // 3. 标记已连接
    // (IResource 无单独的 base::Open()，直接设置状态)

    // 4. *IDN?（对应 OpenTAP SendIDNOnConnect）
    if (sendIDNOnConnect_) {
        idnString_ = queryIdn().trimmed();
        if (!idnString_.isEmpty()) {
            qDebug().noquote() << QString("ScpiInstrument: *IDN? = %1").arg(idnString_);
        }
    }

    // 5. *CLS（对应 OpenTAP SendCLSOnConnect）
    if (sendCLSOnConnect_) {
        commandCls();
        qDebug() << "ScpiInstrument: *CLS sent";
    }

    return true;
}

// ============================================================
// IResource: close() — 关闭连接
// ============================================================
void ScpiInstrument::close() {
    if (io_) {
        QMutexLocker lock(&commandLock_);
        io_->close();
        delete io_;
        io_ = nullptr;
    }
    idnString_.clear();
}

// ============================================================
// scpiCommand / scpiQuery — 线程安全通信（参考 OpenTAP commandLock）
// ============================================================

bool ScpiInstrument::scpiCommand(const QString& cmd, int timeoutMs) {
    QMutexLocker lock(&commandLock_);
    if (!io_ || !io_->isConnected()) return false;

    QElapsedTimer timer;
    if (verboseLogging_) timer.start();

    bool ok = io_->writeCommand(cmd, timeoutMs);

    if (verboseLogging_) {
        qDebug().noquote() << QString("SCPI >> %1 (%2ms)")
            .arg(cmd).arg(timer.elapsed());
    }

    // 可选的命令后错误检查（对应 OpenTAP QueryErrorAfterCommand）
    if (ok && queryErrorAfterCommand_) {
        checkScpiErrors(cmd);
    }

    return ok;
}

QString ScpiInstrument::scpiQuery(const QString& query, int timeoutMs) {
    QMutexLocker lock(&commandLock_);
    if (!io_ || !io_->isConnected()) return {};

    QElapsedTimer timer;
    if (verboseLogging_) timer.start();

    QString result = io_->query(query, timeoutMs);

    if (verboseLogging_) {
        qDebug().noquote() << QString("SCPI >> %1 << %2 (%3ms)")
            .arg(query, result.isEmpty() ? "<empty>" : result)
            .arg(timer.elapsed());
    }

    // 可选的查询后错误检查
    if (queryErrorAfterCommand_) {
        checkScpiErrors(query);
    }

    return result;
}

// ============================================================
// 错误检查（对应 OpenTAP queryErrors）
// ============================================================

void ScpiInstrument::checkScpiErrors(const QString& cmdContext) {
    if (!io_) return;
    QString err = io_->readError();
    if (!err.isEmpty() && err != "0,\"No error\"") {
        qWarning().noquote()
            << QString("ScpiInstrument: SCPI error after '%1': %2")
                   .arg(cmdContext, err);
        // OpenTAP 会抛 VISAException，EonTest 先记录 warning 保持宽松
    }
}

bool ScpiInstrument::commandCls() {
    QMutexLocker lock(&commandLock_);
    if (!io_ || !io_->isConnected()) return false;
    return io_->writeCommand("*CLS");
}

QString ScpiInstrument::queryIdn() {
    QMutexLocker lock(&commandLock_);
    if (!io_ || !io_->isConnected()) return {};
    return io_->query("*IDN?", ioTimeoutMs_);
}

// ============================================================
// 参数读取工具方法
// ============================================================

QString ScpiInstrument::param(const QVariantMap& data, const QString& key, const QString& fallback) const {
    return data.value(key, fallback).toString().trimmed();
}

int ScpiInstrument::paramInt(const QVariantMap& data, const QString& key, int fallback) const {
    return data.value(key, fallback).toInt();
}

double ScpiInstrument::paramDouble(const QVariantMap& data, const QString& key, double fallback) const {
    return data.value(key, fallback).toDouble();
}

} // namespace eon::sdk
