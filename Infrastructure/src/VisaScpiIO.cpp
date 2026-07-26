#include "eon/infra/VisaScpiIO.h"
#include <QLibrary>
#include <QCoreApplication>
#include <QDebug>

namespace eon::infra {

// ============================================================
// 构造 / 析构
// ============================================================

VisaScpiIO::VisaScpiIO() {
    loadVisaLibrary();
}

VisaScpiIO::~VisaScpiIO() {
    close();
}

// ============================================================
// 动态加载 VISA 库
// ============================================================

bool VisaScpiIO::loadVisaLibrary() {
    if (visaLoaded_) return true;

#ifdef Q_OS_WIN
    const QStringList libNames = { "visa32.dll", "visa64.dll" };
#else
    const QStringList libNames = { "libvisa.so", "libvisa.so.0" };
#endif

    QLibrary* lib = nullptr;
    for (const auto& name : libNames) {
        lib = new QLibrary(name);
        if (lib->load()) break;
        delete lib;
        lib = nullptr;
    }

    if (!lib) {
        qWarning() << "VisaScpiIO: VISA library not found. Tried:" << libNames;
        return false;
    }

    pOpenDefaultRM = reinterpret_cast<viOpenDefaultRM_t>(lib->resolve("viOpenDefaultRm"));
    pOpen          = reinterpret_cast<viOpen_t>(lib->resolve("viOpen"));
    pClose         = reinterpret_cast<viClose_t>(lib->resolve("viClose"));
    pWrite         = reinterpret_cast<viWrite_t>(lib->resolve("viWrite"));
    pRead          = reinterpret_cast<viRead_t>(lib->resolve("viRead"));
    pClear         = reinterpret_cast<viClear_t>(lib->resolve("viClear"));
    pStatusDesc    = reinterpret_cast<viStatusDesc_t>(lib->resolve("viStatusDesc"));
    pSetAttribute  = reinterpret_cast<viSetAttribute_t>(lib->resolve("viSetAttribute"));

    if (!pOpenDefaultRM || !pOpen || !pClose || !pWrite || !pRead) {
        qWarning() << "VisaScpiIO: failed to resolve VISA functions";
        return false;
    }

    visaLoaded_ = true;
    qDebug() << "VisaScpiIO: VISA library loaded successfully";
    return true;
}

// ============================================================
// 从配置构建 VISA 资源字符串
// ============================================================

QString VisaScpiIO::buildResourceString(const QVariantMap& config) const {
    // 直接指定 resource 字符串
    QString rsrc = config.value("resource", config.value("visaResource", "")).toString().trimmed();
    if (!rsrc.isEmpty()) return rsrc;

    // 从分散字段构建
    QString ct = config.value("connectType", "").toString().toLower();

    // 串口: ASRL::COM5::INSTR
    if (ct == "serial" || ct == "com" || ct == "rs232" || config.contains("port")) {
        QString port = config.value("port", "COM1").toString();
        if (port.startsWith("COM", Qt::CaseInsensitive)) {
            return QString("ASRL%1::INSTR").arg(port.mid(3));
        }
        return QString("ASRL::%1::INSTR").arg(port);
    }

    // TCP/IP: TCPIP::192.168.1.1::5025::SOCKET
    if (ct == "tcp" || ct == "tcpip" || ct == "lan" || config.contains("host")) {
        QString host = config.value("host", "192.168.1.1").toString();
        int port = config.value("tcpPort", config.value("port", 5025)).toInt();
        return QString("TCPIP::%1::%2::SOCKET").arg(host).arg(port);
    }

    // GPIB: GPIB::5::INSTR
    if (ct == "gpib" || config.contains("gpibAddress")) {
        int addr = config.value("gpibAddress", config.value("gpib", 5)).toInt();
        return QString("GPIB::%1::INSTR").arg(addr);
    }

    // USB: USB::0x1234::0x5678::SN::INSTR
    if (ct == "usb" || ct == "usbtmc") {
        QString vid = config.value("vendorId", config.value("vid", "")).toString();
        QString pid = config.value("productId", config.value("pid", "")).toString();
        QString sn  = config.value("serialNumber", config.value("serial", "*")).toString();
        return QString("USB::%1::%2::%3::INSTR").arg(vid, pid, sn);
    }

    // 尝试把 port/address 直接当资源字符串
    QString port = config.value("port", config.value("address", "")).toString().trimmed();
    if (!port.isEmpty() && port.contains("::")) return port;

    return {};
}

// ============================================================
// IScpiIO 接口实现
// ============================================================

bool VisaScpiIO::open(const QVariantMap& config) {
    if (!visaLoaded_) return false;
    close();

    resourceString_ = buildResourceString(config);
    if (resourceString_.isEmpty()) {
        qWarning() << "VisaScpiIO: cannot build resource string from config";
        return false;
    }

    openTimeoutMs_ = config.value("openTimeoutMs", config.value("visaTimeout", 5000)).toInt();
    vioTimeoutMs_ = config.value("timeoutMs", config.value("visaTimeout", 5000)).toInt();

    ViStatus st = pOpenDefaultRM(&defaultRM_);
    if (st != VI_SUCCESS) {
        qWarning() << "VisaScpiIO: viOpenDefaultRM failed:" << visaStatusString(st);
        defaultRM_ = VI_NULL;
        return false;
    }

    st = pOpen(defaultRM_, resourceString_.toUtf8().constData(), 0, openTimeoutMs_, &instr_);
    if (st != VI_SUCCESS) {
        qWarning() << "VisaScpiIO: viOpen" << resourceString_ << "failed:" << visaStatusString(st);
        instr_ = VI_NULL;
        return false;
    }

    // 设置 VISA I/O 超时
    if (pSetAttribute) {
        ViStatus tmoSt = pSetAttribute(instr_, VI_ATTR_TMO_VALUE, (ViAttrState)vioTimeoutMs_);
        if (tmoSt != VI_SUCCESS) {
            qWarning() << "VisaScpiIO: viSetAttribute(TMO) failed:" << visaStatusString(tmoSt);
        }
    }

    qDebug() << "VisaScpiIO: opened" << resourceString_ << "timeout=" << vioTimeoutMs_ << "ms";
    return true;
}

void VisaScpiIO::close() {
    if (instr_ != VI_NULL && pClose) {
        pClose(instr_);
        instr_ = VI_NULL;
    }
    if (defaultRM_ != VI_NULL && pClose) {
        pClose(defaultRM_);
        defaultRM_ = VI_NULL;
    }
    resourceString_.clear();
}

bool VisaScpiIO::isConnected() const {
    return instr_ != VI_NULL;
}

bool VisaScpiIO::deviceClear() {
    if (!isConnected() || !pClear) return false;
    return pClear(instr_) == VI_SUCCESS;
}

bool VisaScpiIO::writeCommand(const QString& cmd, int timeoutMs) {
    if (!isConnected() || !pWrite) return false;

    // 设置写入超时
    if (pSetAttribute) {
        pSetAttribute(instr_, VI_ATTR_TMO_VALUE, (ViAttrState)timeoutMs);
    }

    QByteArray buf = (cmd + "\n").toUtf8();
    ViUInt32 written = 0;
    ViStatus st = pWrite(instr_, reinterpret_cast<ViBuf>(buf.data()),
                         static_cast<ViUInt32>(buf.size()), &written);
    return st == VI_SUCCESS;
}

QString VisaScpiIO::query(const QString& query, int timeoutMs) {
    if (!isConnected() || !pWrite || !pRead) return {};

    // 设置 I/O 超时
    if (pSetAttribute) {
        pSetAttribute(instr_, VI_ATTR_TMO_VALUE, (ViAttrState)timeoutMs);
    }

    // 写查询命令
    QByteArray cmdBuf = (query + "\n").toUtf8();
    ViUInt32 written = 0;
    ViStatus st = pWrite(instr_, reinterpret_cast<ViBuf>(cmdBuf.data()),
                         static_cast<ViUInt32>(cmdBuf.size()), &written);
    if (st != VI_SUCCESS) return {};

    // 读响应
    QByteArray buf;
    buf.resize(4096);
    ViUInt32 readCount = 0;
    st = pRead(instr_, reinterpret_cast<ViBuf>(buf.data()),
               static_cast<ViUInt32>(buf.size()), &readCount);
    if (st != VI_SUCCESS && readCount == 0) return {};

    buf.truncate(static_cast<int>(readCount));
    return QString::fromUtf8(buf).trimmed();
}

QString VisaScpiIO::readError() {
    return query("SYST:ERR?").trimmed();
}

QString VisaScpiIO::configInfo() const {
    if (!visaLoaded_)
        return QString("VisaScpiIO [VISA not loaded]");
    if (instr_ == VI_NULL)
        return QString("VisaScpiIO [closed]");
    return QString("VisaScpiIO [%1]").arg(resourceString_);
}

// ============================================================
// 辅助
// ============================================================

QString VisaScpiIO::visaStatusString(ViStatus st) const {
    if (!pStatusDesc) return QString("0x%1").arg(st, 0, 16);
    ViChar desc[256] = {};
    pStatusDesc(VI_NULL, st, desc);
    return QString::fromUtf8(desc);
}

// ============================================================
// ITransport 接口
// ============================================================

QByteArray VisaScpiIO::readBytes(int timeoutMs)
{
    if (!isConnected() || !pRead) return {};
    if (pSetAttribute)
        pSetAttribute(instr_, VI_ATTR_TMO_VALUE, (ViAttrState)timeoutMs);
    QByteArray buf;
    buf.resize(4096);
    ViUInt32 readCount = 0;
    ViStatus st = pRead(instr_, reinterpret_cast<ViBuf>(buf.data()),
                        static_cast<ViUInt32>(buf.size()), &readCount);
    if (st != VI_SUCCESS && readCount == 0) return {};
    buf.truncate(static_cast<int>(readCount));
    return buf;
}

bool VisaScpiIO::writeBytes(const QByteArray& data, int timeoutMs)
{
    if (!isConnected() || !pWrite) return false;
    if (pSetAttribute)
        pSetAttribute(instr_, VI_ATTR_TMO_VALUE, (ViAttrState)timeoutMs);
    ViUInt32 written = 0;
    ViStatus st = pWrite(instr_, reinterpret_cast<ViBuf>(const_cast<char*>(data.constData())),
                         static_cast<ViUInt32>(data.size()), &written);
    return st == VI_SUCCESS;
}

} // namespace eon::infra
