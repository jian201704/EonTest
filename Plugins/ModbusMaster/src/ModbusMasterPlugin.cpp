#include <QObject>
#include <QVariantMap>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "eon/sdk/IStepPlugin.h"
#include "eon/infra/ResponseDecoder.h"
#include "Transports.h"

namespace {

struct ModbusConnection {
    std::unique_ptr<ModbusHandle> handle;
    std::set<QString> workflows;
};

std::map<QString, std::unique_ptr<ModbusConnection>> s_connections;
std::mutex s_connectionsMutex;

QString connectionKey(const QVariantMap& data, const QString& transport) {
    if (transport == "tcp" || transport == "ethernet" || transport == "network") {
        return QString("tcp:%1:%2").arg(
            data.value("host", data.value("ip", "192.168.1.100")).toString(),
            QString::number(data.value("port", data.value("tcpPort", 502)).toInt()));
    }
    return QString("serial:%1:%2:%3:%4:%5")
        .arg(data.value("port", data.value("comPort", "COM3")).toString())
        .arg(data.value("baudRate", data.value("baud", 9600)).toInt())
        .arg(data.value("dataBits", 8).toInt())
        .arg(data.value("parity", "none").toString().toLower())
        .arg(data.value("stopBits", 1).toInt());
}

QVariantList loadDecodeProfile(const QVariant& value) {
    const QString path = value.toString().trimmed();
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (document.isArray()) return document.array().toVariantList();
    if (document.isObject() && document.object().value("measurements").isArray())
        return document.object().value("measurements").toArray().toVariantList();
    return {};
}

bool decodeModbusValues(const QVariantList& values, const QVariantMap& data,
                        QVariantMap& output, QString& errorMessage) {
    QVariantList specs = data.value("decodeSpecs").toList();
    if (specs.isEmpty()) specs = loadDecodeProfile(data.value("decodeProfile"));
    const auto single = eon::infra::DecodeSpec::fromVariantMap(data);
    if (specs.isEmpty() && !single.hasExplicitDecode()) return true;

    QByteArray payload;
    for (const auto& item : values) {
        const quint16 value = static_cast<quint16>(item.toUInt());
        payload.append(static_cast<char>((value >> 8) & 0xff));
        payload.append(static_cast<char>(value & 0xff));
    }
    const auto decoded = specs.isEmpty()
        ? eon::infra::ResponseDecoder::decode(payload, single)
        : eon::infra::ResponseDecoder::decodeMany(payload, specs);
    output["rawResponseHex"] = payload.toHex(' ').toUpper();
    output["resultItems"] = decoded.toVariantList();
    output["measuredSamples"] = decoded.toVariantList();
    if (!decoded.success) {
        errorMessage = QString("Modbus response decode failed: %1").arg(decoded.errorMessage);
        return false;
    }
    if (!decoded.measurements.isEmpty()) {
        const auto& first = decoded.measurements.first();
        output["measuredValue"] = first.value;
        output["measuredUnit"] = first.unit;
        output["measurementName"] = first.name;
    }
    return true;
}

void applyLimits(QVariantMap& data) {
    if (!data.contains("measuredValue") ||
        (!data.contains("lowerLimit") && !data.contains("upperLimit"))) return;
    const double value = data.value("measuredValue").toDouble();
    const bool hasLower = data.contains("lowerLimit");
    const bool hasUpper = data.contains("upperLimit");
    const double lower = data.value("lowerLimit").toDouble();
    const double upper = data.value("upperLimit").toDouble();
    const bool passed = (!hasLower || value >= lower) && (!hasUpper || value <= upper);
    data["resultText"] = passed ? "PASS" : "FAIL";
    data["analyze.passed"] = passed;
    data["analyze.value"] = value;
    if (hasLower) data["analyze.min"] = lower;
    if (hasUpper) data["analyze.max"] = upper;
    data["analyze.unit"] = data.value("measuredUnit");
}

} // namespace

class ModbusMasterPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "modbusmaster.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override { return "modbus.master"; }

    void postWorkflow(eon::sdk::WorkflowContext& context) override {
        const QString workflowKey = QString("%1/%2").arg(
            context.workflowId, context.data.value("_cellId", "default").toString());
        std::lock_guard lock(s_connectionsMutex);
        for (auto it = s_connections.begin(); it != s_connections.end();) {
            it->second->workflows.erase(workflowKey);
            if (it->second->workflows.empty()) {
                if (it->second->handle) it->second->handle->close();
                it = s_connections.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        auto& d = context.data;
        const bool virt = d.value("virtualMode", false).toBool();
        const QString action = d.value("action", "read").toString().trimmed().toLower();
        const int funcCode = d.value("functionCode", d.value("func", 3)).toInt();
        const uint8_t slaveId = static_cast<uint8_t>(d.value("slaveId", d.value("address", 1)).toUInt());
        const uint16_t startAddr = static_cast<uint16_t>(d.value("startAddr", d.value("addr", 0)).toUInt());
        const uint16_t quantity = static_cast<uint16_t>(d.value("quantity", d.value("count", 1)).toUInt());

        // 虚拟模式：模拟寄存器值，同时做限值对比
        if (virt) {
            const double simValue = d.value("simValue", 1234.0).toDouble();
            d.insert("modbus.raw", "(virtual)");
            d.insert("modbus.values", QVariantList{simValue, 5678, 910});
            d.insert("measuredValue", simValue);
            d.insert("measuredUnit", d.value("unit", "counts").toString());
            d.insert("modbus.slaveId", slaveId);
            d.insert("modbus.functionCode", funcCode);
            QString decodeError;
            if (!decodeModbusValues(d.value("modbus.values").toList(), d, d, decodeError)) {
                errorMessage = decodeError;
                return false;
            }
            applyLimits(d);
            return true;
        }

        const QString transport = d.value("transport", d.value("connType", "serial")).toString().toLower();
        const int timeoutMs = d.value("timeoutMs", d.value("timeout", 3000)).toInt();

        const QString key = connectionKey(d, transport);
        const QString workflowKey = QString("%1/%2").arg(
            context.workflowId, d.value("_cellId", "default").toString());
        ModbusConnection* connection = nullptr;
        {
            std::lock_guard lock(s_connectionsMutex);
            auto& slot = s_connections[key];
            if (!slot) slot = std::make_unique<ModbusConnection>();
            if (!slot->handle) slot->handle = std::make_unique<ModbusHandle>();
            slot->workflows.insert(workflowKey);
            connection = slot.get();
        }
        std::unique_lock ioLock(connection->handle->ioMutex);
        auto& handle = *connection->handle;
        handle.slaveId = slaveId;
        handle.unitId = slaveId;

        if (transport == "tcp" || transport == "ethernet" || transport == "network") {
            const QString host = d.value("host", d.value("ip", "192.168.1.100")).toString();
            const int port = d.value("port", d.value("tcpPort", 502)).toInt();
            if (!handle.tcp && !handle.openTCP(host, port)) {
                errorMessage = QString("Cannot connect to Modbus TCP %1:%2").arg(host).arg(port);
                return false;
            }
        } else {
            // Serial RTU (default)
            const QString portName = d.value("port", d.value("comPort", "COM3")).toString();
            const int baud = d.value("baudRate", d.value("baud", 9600)).toInt();
            const int dataBits = d.value("dataBits", 8).toInt();
            const QString parity = d.value("parity", "none").toString();
            const int stopBits = d.value("stopBits", 1).toInt();
            if (!handle.serial && !handle.openSerial(portName, baud, dataBits, parity, stopBits)) {
                errorMessage = QString("Cannot open Modbus RTU %1 @ %2").arg(portName).arg(baud);
                return false;
            }
        }

        bool ok = false;
        QVariantList values;

        switch (funcCode) {
        case 1:  ok = readCoils(handle, startAddr, quantity, values, timeoutMs, errorMessage); break;
        case 2:  ok = readDiscreteInputs(handle, startAddr, quantity, values, timeoutMs, errorMessage); break;
        case 3:  ok = readHoldingRegisters(handle, startAddr, quantity, values, timeoutMs, errorMessage); break;
        case 4:  ok = readInputRegisters(handle, startAddr, quantity, values, timeoutMs, errorMessage); break;
        case 5:  ok = writeSingleCoil(handle, startAddr, d.value("value", 0xFF00).toUInt(), timeoutMs, errorMessage); break;
        case 6:  ok = writeSingleRegister(handle, startAddr, d.value("value", 0).toUInt(), timeoutMs, errorMessage); break;
        case 15: ok = writeMultipleCoils(handle, startAddr, d.value("values").toList(), timeoutMs, errorMessage); break;
        case 16: ok = writeMultipleRegisters(handle, startAddr, d.value("values").toList(), timeoutMs, errorMessage); break;
        default:
            errorMessage = QString("Unsupported Modbus function code: %1").arg(funcCode);
            ok = false;
        }

        if (!ok) return false;

        // 限值对比：如果配置了下限/上限，自动判定 PASS/FAIL
        d.insert("modbus.values", QVariant::fromValue(values));
        d.insert("modbus.slaveId", slaveId);
        d.insert("modbus.functionCode", funcCode);

        if (!decodeModbusValues(values, d, d, errorMessage)) return false;
        applyLimits(d);

        if (!values.isEmpty()) {
            const double val = values.first().toDouble();
            d.insert("measuredValue", val);
            d.insert("measuredUnit", d.value("unit", "counts").toString());

            // 读取限值（来自 XLSX 列 10~11 或 config 键值对）
            const bool hasLower = d.contains("lowerLimit");
            const bool hasUpper = d.contains("upperLimit");
            if (hasLower || hasUpper) {
                const double lo = d.value("lowerLimit").toDouble();
                const double hi = d.value("upperLimit").toDouble();
                const bool passed = (!hasLower || val >= lo) && (!hasUpper || val <= hi);
                d.insert("resultText", passed ? "PASS" : "FAIL");
                d.insert("analyze.passed", passed);
                d.insert("analyze.value", val);
                if (hasLower) d.insert("analyze.min", lo);
                if (hasUpper) d.insert("analyze.max", hi);
                d.insert("analyze.unit", d.value("measuredUnit"));
                d.insert("analyze.message",
                    passed
                    ? QString("PASS: %1 within [%2, %3]").arg(val).arg(lo).arg(hi)
                    : QString("FAIL: %1 outside [%2, %3]").arg(val).arg(lo).arg(hi));
            }
        }
        return true;
    }

private:
    // --- Read Coils (FC 01) ---
    bool readCoils(ModbusHandle& h, uint16_t addr, uint16_t count,
                   QVariantList& values, int timeout, QString& error) {
        QByteArray pdu;
        pdu.append(static_cast<char>(0x01));           // function code
        pdu.append(static_cast<char>((addr >> 8) & 0xFF));
        pdu.append(static_cast<char>(addr & 0xFF));
        pdu.append(static_cast<char>((count >> 8) & 0xFF));
        pdu.append(static_cast<char>(count & 0xFF));

        if (!h.writeFrame(pdu, timeout)) { error = "Write failed"; return false; }
        QByteArray resp = h.readResponse(timeout);
        if (resp.isEmpty()) { error = "No response"; return false; }

        int offset = (h.type == ModbusTransportType::SerialRTU) ? 0 : 7;
        if (h.type == ModbusTransportType::SerialRTU && resp.size() < 5) { error = "Response too short"; return false; }
        if (h.type == ModbusTransportType::TCP && resp.size() < 12) { error = "Response too short"; return false; }

        // 异常码检查 (func | 0x80)
        if (resp[offset] & 0x80) {
            error = QString("Modbus exception 0x%1").arg(static_cast<uint8_t>(resp[offset + 1]), 2, 16, QChar('0'));
            return false;
        }

        int byteCount = static_cast<uint8_t>(resp[offset + 1]);
        int dataStart = offset + 2;

        for (int i = 0; i < byteCount; ++i) {
            uint8_t byte = static_cast<uint8_t>(resp[dataStart + i]);
            for (int b = 0; b < 8 && values.size() < count; ++b)
                values.append(static_cast<bool>(byte & (1 << b)));
        }
        return true;
    }

    // --- Read Discrete Inputs (FC 02) ---
    bool readDiscreteInputs(ModbusHandle& h, uint16_t addr, uint16_t count,
                            QVariantList& values, int timeout, QString& error) {
        QByteArray pdu;
        pdu.append(static_cast<char>(0x02));
        pdu.append(static_cast<char>((addr >> 8) & 0xFF));
        pdu.append(static_cast<char>(addr & 0xFF));
        pdu.append(static_cast<char>((count >> 8) & 0xFF));
        pdu.append(static_cast<char>(count & 0xFF));
        if (!h.writeFrame(pdu, timeout)) { error = "Write failed"; return false; }
        QByteArray resp = h.readResponse(timeout);
        if (resp.isEmpty()) { error = "No response"; return false; }

        int offset = (h.type == ModbusTransportType::SerialRTU) ? 0 : 7;
        int byteCount = static_cast<uint8_t>(resp[offset + 1]);
        int dataStart = offset + 2;

        for (int i = 0; i < byteCount && i < resp.size() - dataStart; ++i) {
            uint8_t byte = static_cast<uint8_t>(resp[dataStart + i]);
            for (int b = 0; b < 8 && values.size() < count; ++b)
                values.append(static_cast<bool>(byte & (1 << b)));
        }
        return true;
    }

    // --- Read Holding Registers (FC 03) ---
    bool readHoldingRegisters(ModbusHandle& h, uint16_t addr, uint16_t count,
                              QVariantList& values, int timeout, QString& error) {
        QByteArray pdu;
        pdu.append(static_cast<char>(0x03));
        pdu.append(static_cast<char>((addr >> 8) & 0xFF));
        pdu.append(static_cast<char>(addr & 0xFF));
        pdu.append(static_cast<char>((count >> 8) & 0xFF));
        pdu.append(static_cast<char>(count & 0xFF));

        if (!h.writeFrame(pdu, timeout)) { error = "Write failed"; return false; }
        QByteArray resp = h.readResponse(timeout);
        if (resp.isEmpty()) { error = "No response"; return false; }

        int offset = (h.type == ModbusTransportType::SerialRTU) ? 0 : 7;
        int byteCount = static_cast<uint8_t>(resp[offset + 1]);
        int dataStart = offset + 2;

        for (int i = 0; i + 1 < byteCount && dataStart + i + 1 < resp.size(); i += 2) {
            uint8_t hi = static_cast<uint8_t>(resp[dataStart + i]);
            uint8_t lo = static_cast<uint8_t>(resp[dataStart + i + 1]);
            values.append(static_cast<double>(toU16(hi, lo)));
        }
        return true;
    }

    // --- Read Input Registers (FC 04) ---
    bool readInputRegisters(ModbusHandle& h, uint16_t addr, uint16_t count,
                            QVariantList& values, int timeout, QString& error) {
        QByteArray pdu;
        pdu.append(static_cast<char>(0x04));
        pdu.append(static_cast<char>((addr >> 8) & 0xFF));
        pdu.append(static_cast<char>(addr & 0xFF));
        pdu.append(static_cast<char>((count >> 8) & 0xFF));
        pdu.append(static_cast<char>(count & 0xFF));
        if (!h.writeFrame(pdu, timeout)) { error = "Write failed"; return false; }
        QByteArray resp = h.readResponse(timeout);
        if (resp.isEmpty()) { error = "No response"; return false; }
        // 解析同 FC 03
        int offset = (h.type == ModbusTransportType::SerialRTU) ? 0 : 7;
        int byteCount = static_cast<uint8_t>(resp[offset + 1]);
        int dataStart = offset + 2;
        for (int i = 0; i + 1 < byteCount && dataStart + i + 1 < resp.size(); i += 2) {
            uint8_t hi = static_cast<uint8_t>(resp[dataStart + i]);
            uint8_t lo = static_cast<uint8_t>(resp[dataStart + i + 1]);
            values.append(static_cast<double>(toU16(hi, lo)));
        }
        return true;
    }

    // --- Write Single Coil (FC 05) ---
    bool writeSingleCoil(ModbusHandle& h, uint16_t addr, uint16_t value,
                         int timeout, QString& error) {
        QByteArray pdu;
        pdu.append(static_cast<char>(0x05));
        pdu.append(static_cast<char>((addr >> 8) & 0xFF));
        pdu.append(static_cast<char>(addr & 0xFF));
        pdu.append(static_cast<char>((value >> 8) & 0xFF));
        pdu.append(static_cast<char>(value & 0xFF)); // 0xFF00=ON, 0x0000=OFF
        if (!h.writeFrame(pdu, timeout)) { error = "Write failed"; return false; }
        QByteArray resp = h.readResponse(timeout);
        if (resp.isEmpty()) { error = "No response"; return false; }
        return true;
    }

    // --- Write Single Register (FC 06) ---
    bool writeSingleRegister(ModbusHandle& h, uint16_t addr, uint16_t value,
                             int timeout, QString& error) {
        QByteArray pdu;
        pdu.append(static_cast<char>(0x06));
        pdu.append(static_cast<char>((addr >> 8) & 0xFF));
        pdu.append(static_cast<char>(addr & 0xFF));
        pdu.append(static_cast<char>((value >> 8) & 0xFF));
        pdu.append(static_cast<char>(value & 0xFF));
        if (!h.writeFrame(pdu, timeout)) { error = "Write failed"; return false; }
        QByteArray resp = h.readResponse(timeout);
        if (resp.isEmpty()) { error = "No response"; return false; }
        return true;
    }

    // --- Write Multiple Coils (FC 15) ---
    bool writeMultipleCoils(ModbusHandle& h, uint16_t addr, const QVariantList& vals,
                            int timeout, QString& error) {
        int count = qMin(vals.size(), 2000);
        int byteCount = (count + 7) / 8;
        QByteArray pdu;
        pdu.append(static_cast<char>(0x0F));
        pdu.append(static_cast<char>((addr >> 8) & 0xFF));
        pdu.append(static_cast<char>(addr & 0xFF));
        pdu.append(static_cast<char>((count >> 8) & 0xFF));
        pdu.append(static_cast<char>(count & 0xFF));
        pdu.append(static_cast<char>(byteCount));
        uint8_t byte = 0;
        for (int i = 0; i < count; ++i) {
            if (vals[i].toBool()) byte |= (1 << (i % 8));
            if ((i % 8) == 7) { pdu.append(static_cast<char>(byte)); byte = 0; }
        }
        if (count % 8 != 0) pdu.append(static_cast<char>(byte));

        if (!h.writeFrame(pdu, timeout)) { error = "Write failed"; return false; }
        QByteArray resp = h.readResponse(timeout);
        if (resp.isEmpty()) { error = "No response"; return false; }
        return true;
    }

    // --- Write Multiple Registers (FC 16) ---
    bool writeMultipleRegisters(ModbusHandle& h, uint16_t addr, const QVariantList& vals,
                                int timeout, QString& error) {
        int count = qMin(vals.size(), 123);
        QByteArray pdu;
        pdu.append(static_cast<char>(0x10));
        pdu.append(static_cast<char>((addr >> 8) & 0xFF));
        pdu.append(static_cast<char>(addr & 0xFF));
        pdu.append(static_cast<char>((count >> 8) & 0xFF));
        pdu.append(static_cast<char>(count & 0xFF));
        pdu.append(static_cast<char>(static_cast<uint8_t>(count * 2)));

        for (int i = 0; i < count; ++i) {
            uint16_t v = static_cast<uint16_t>(vals[i].toUInt());
            pdu.append(static_cast<char>((v >> 8) & 0xFF));
            pdu.append(static_cast<char>(v & 0xFF));
        }

        if (!h.writeFrame(pdu, timeout)) { error = "Write failed"; return false; }
        QByteArray resp = h.readResponse(timeout);
        if (resp.isEmpty()) { error = "No response"; return false; }
        return true;
    }
};

#include "ModbusMasterPlugin.moc"
