#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#ifdef EON_HAS_SERIALPORT
#include <QSerialPort>
#else
#include <windows.h>
#include <string>
#endif

#include "eon/sdk/IStepPlugin.h"

// ============================================================================
// MultimeterPlugin — 数字万用表测量插件（SCPI 串口/GPIB 通信）
//
// context.data 参数：
//   dmmPort          - 串口名，如 "COM4"
//   dmmBaudRate      - 波特率（默认 9600）
//   dmmMeasureType   - 测量类型："VOLTage:DC"/"CURRent:DC"/"RESistance"
//   dmmRange         - 量程（AUTO 或数值）
//   dmmResolution    - 分辨率（默认 0.001）
//   dmmSamples       - 采样次数取平均（默认 1）
//
// 输出到 context.data：
//   measuredValue     - 测量值 (double)
//   measuredUnit      - 单位 ("V"/"A"/"Ohm")
//   measuredSamples   - 有效采样数
//   measuredTimestamp - ISO 时间戳
//   dmmIdent          - *IDN? 响应
// ============================================================================

#ifdef EON_HAS_SERIALPORT
// ======================== Qt SerialPort 实现 ========================

class MultimeterPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "multimeter.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override { return "measure.voltage"; }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        const bool virtualMode = context.data.value("virtualMode", false).toBool();
        const QString portName = context.data.value("dmmPort", "COM4").toString();
        const int baudRate = context.data.value("dmmBaudRate", 9600).toInt();
        const QString measureType = context.data.value("dmmMeasureType", "VOLTage:DC").toString();
        const QString range = context.data.value("dmmRange", "AUTO").toString();
        const double resolution = context.data.value("dmmResolution", 0.001).toDouble();
        const int samples = qBound(1, context.data.value("dmmSamples", 1).toInt(), 100);
        QStringList trace;
        auto appendTrace = [&](const QString& entry) {
            trace.append(entry);
            context.data.insert("dmmScpiTrace", trace.join(" | "));
        };
        auto setErrorWithTrace = [&](const QString& msg) {
            errorMessage = trace.isEmpty() ? msg : QString("%1 | trace: %2").arg(msg, trace.join(" | "));
        };

        appendTrace(QString("OPEN %1 @ %2").arg(portName).arg(baudRate));

        if (virtualMode || portName.isEmpty() || portName.compare("VIRTUAL", Qt::CaseInsensitive) == 0) {
            const double virtualValue = context.data.value(
                "virtualMeasuredValue",
                context.data.value("powerActualVoltage", context.data.value("powerVoltage", 3.3))
            ).toDouble();
            context.data.insert("dmmIdent", "VIRTUAL-DMM");
            context.data.insert("measuredValue", virtualValue);
            context.data.insert("measuredUnit", measureType.contains("CURR", Qt::CaseInsensitive) ? "A"
                                                     : measureType.contains("RES", Qt::CaseInsensitive) ? "Ohm"
                                                     : "V");
            context.data.insert("measuredSamples", 1);
            context.data.insert("measuredTimestamp",
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            context.data.insert("measurementStatus", "virtual");
            appendTrace("VIRTUAL MODE");
            return true;
        }

        QSerialPort port;
        port.setPortName(portName);
        port.setBaudRate(baudRate);
        port.setDataBits(QSerialPort::Data8);
        port.setParity(QSerialPort::NoParity);
        port.setStopBits(QSerialPort::OneStop);
        port.setFlowControl(QSerialPort::NoFlowControl);

        if (!port.open(QIODevice::ReadWrite)) {
            setErrorWithTrace(QString("Multimeter: Cannot open %1: %2")
                .arg(portName, port.errorString()));
            return false;
        }
        appendTrace(QString("OPEN OK"));

        appendTrace(">> *IDN?");
        const QString idn = scpiQuery(port, "*IDN?", 500);
        appendTrace(QString("<< %1").arg(idn.isEmpty() ? QString("<empty>") : idn));
        if (idn.isEmpty()) {
            setErrorWithTrace("Multimeter: Device not responding to *IDN?");
            port.close();
            return false;
        }
        context.data.insert("dmmIdent", idn);

        const QString configCmd = QString("CONFigure:%1 %2,%3")
            .arg(measureType, range).arg(resolution, 0, 'f', 4);
        appendTrace(QString(">> %1").arg(configCmd));
        scpiCommand(port, configCmd, 500);

        double sum = 0.0;
        int validSamples = 0;
        for (int i = 0; i < samples; ++i) {
            appendTrace(QString(">> READ? (#%1)").arg(i + 1));
            const QString resp = scpiQuery(port, "READ?", 2000);
            appendTrace(QString("<< %1").arg(resp.isEmpty() ? QString("<empty>") : resp.trimmed()));
            if (!resp.isEmpty()) {
                bool ok = false;
                const double val = resp.trimmed().toDouble(&ok);
                if (ok) { sum += val; validSamples++; }
            }
        }
        port.close();

        if (validSamples == 0) {
            setErrorWithTrace("Multimeter: All measurement samples failed.");
            return false;
        }

        return storeResult(context, measureType, sum / validSamples, validSamples);
    }

private:
    static bool scpiCommand(QSerialPort& port, const QString& cmd, int timeoutMs) {
        const QByteArray data = (cmd + "\r\n").toUtf8();
        port.write(data);
        return port.waitForBytesWritten(timeoutMs);
    }

    static QString scpiQuery(QSerialPort& port, const QString& cmd, int timeoutMs = 1000) {
        port.write((cmd + "\n").toUtf8());
        if (!port.waitForBytesWritten(200)) return {};
        if (!port.waitForReadyRead(timeoutMs)) return {};
        QByteArray buf = port.readAll();
        int nl = buf.indexOf('\n');
        if (nl >= 0) buf.truncate(nl);
        return QString::fromUtf8(buf).trimmed();
    }

    static bool storeResult(eon::sdk::WorkflowContext& context,
                            const QString& measureType, double value, int samples) {
        QString unit = "V";
        if (measureType.contains("CURR", Qt::CaseInsensitive)) unit = "A";
        else if (measureType.contains("RES", Qt::CaseInsensitive)) unit = "Ohm";
        context.data.insert("measuredValue", value);
        context.data.insert("measuredUnit", unit);
        context.data.insert("measuredSamples", samples);
        context.data.insert("measuredTimestamp",
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        return true;
    }
};

#else // !EON_HAS_SERIALPORT
// ======================== Win32 API 实现 ========================

class MultimeterPlugin final : public QObject, public eon::sdk::IStepPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EON_ISTEPPLUGIN_IID FILE "multimeter.json")
    Q_INTERFACES(eon::sdk::IStepPlugin)

public:
    QString id() const override { return "measure.voltage"; }

    bool executeStep(eon::sdk::WorkflowContext& context, QString& errorMessage) override {
        const bool virtualMode = context.data.value("virtualMode", false).toBool();
        const QString portName = context.data.value("dmmPort", "COM4").toString();
        const int baudRate = context.data.value("dmmBaudRate", 9600).toInt();
        const QString measureType = context.data.value("dmmMeasureType", "VOLTage:DC").toString();
        const QString range = context.data.value("dmmRange", "AUTO").toString();
        const double resolution = context.data.value("dmmResolution", 0.001).toDouble();
        const int samples = qBound(1, context.data.value("dmmSamples", 1).toInt(), 100);
        QStringList trace;
        auto appendTrace = [&](const QString& entry) {
            trace.append(entry);
            context.data.insert("dmmScpiTrace", trace.join(" | "));
        };
        auto setErrorWithTrace = [&](const QString& msg) {
            errorMessage = trace.isEmpty() ? msg : QString("%1 | trace: %2").arg(msg, trace.join(" | "));
        };

        appendTrace(QString("OPEN %1 @ %2").arg(portName).arg(baudRate));

        if (virtualMode || portName.isEmpty() || portName.compare("VIRTUAL", Qt::CaseInsensitive) == 0) {
            const double virtualValue = context.data.value(
                "virtualMeasuredValue",
                context.data.value("powerActualVoltage", context.data.value("powerVoltage", 3.3))
            ).toDouble();
            context.data.insert("dmmIdent", "VIRTUAL-DMM");
            context.data.insert("measuredValue", virtualValue);
            context.data.insert("measuredUnit", measureType.contains("CURR", Qt::CaseInsensitive) ? "A"
                                                     : measureType.contains("RES", Qt::CaseInsensitive) ? "Ohm"
                                                     : "V");
            context.data.insert("measuredSamples", 1);
            context.data.insert("measuredTimestamp",
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            context.data.insert("measurementStatus", "virtual");
            appendTrace("VIRTUAL MODE");
            return true;
        }

        const std::wstring portPath = (L"\\\\.\\" + portName.toStdWString());
        HANDLE hPort = CreateFileW(portPath.c_str(),
            GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hPort == INVALID_HANDLE_VALUE) {
            setErrorWithTrace(QString("Multimeter: Cannot open %1 (err=%2)")
                .arg(portName).arg(GetLastError()));
            return false;
        }
        appendTrace("OPEN OK");

        DCB dcb = { sizeof(DCB) };
        GetCommState(hPort, &dcb);
        dcb.BaudRate = baudRate;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(hPort, &dcb);

        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 1000;
        timeouts.WriteTotalTimeoutConstant = 500;
        SetCommTimeouts(hPort, &timeouts);

        auto scpiCmd = [&](const QString& cmd) -> bool {
            QByteArray data = (cmd + "\n").toUtf8();
            DWORD written = 0;
            return WriteFile(hPort, data.constData(),
                static_cast<DWORD>(data.size()), &written, nullptr) && written > 0;
        };

        auto scpiRead = [&](int timeoutMs = 1000) -> QString {
            char buf[1024] = {};
            DWORD read = 0;
            Sleep(timeoutMs);
            ReadFile(hPort, buf, sizeof(buf) - 1, &read, nullptr);
            if (read > 0) return QString::fromUtf8(buf, static_cast<int>(read)).trimmed();
            return {};
        };

        appendTrace(">> *IDN?");
        scpiCmd("*IDN?");
        const QString idn = scpiRead(500);
        appendTrace(QString("<< %1").arg(idn.isEmpty() ? QString("<empty>") : idn));
        context.data.insert("dmmIdent", idn);

        const QString configCmd = QString("CONFigure:%1 %2,%3")
            .arg(measureType, range).arg(resolution, 0, 'f', 4);
        appendTrace(QString(">> %1").arg(configCmd));
        scpiCmd(configCmd);

        double sum = 0.0;
        int validSamples = 0;
        for (int i = 0; i < samples; ++i) {
            appendTrace(QString(">> READ? (#%1)").arg(i + 1));
            scpiCmd("READ?");
            const QString resp = scpiRead(2000);
            appendTrace(QString("<< %1").arg(resp.isEmpty() ? QString("<empty>") : resp.trimmed()));
            if (!resp.isEmpty()) {
                bool ok = false;
                const double val = resp.trimmed().toDouble(&ok);
                if (ok) { sum += val; validSamples++; }
            }
        }

        CloseHandle(hPort);

        if (validSamples == 0) {
            setErrorWithTrace("Multimeter: All measurement samples failed.");
            return false;
        }

        QString unit = "V";
        if (measureType.contains("CURR", Qt::CaseInsensitive)) unit = "A";
        else if (measureType.contains("RES", Qt::CaseInsensitive)) unit = "Ohm";

        context.data.insert("measuredValue", sum / validSamples);
        context.data.insert("measuredUnit", unit);
        context.data.insert("measuredSamples", validSamples);
        context.data.insert("measuredTimestamp",
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

        return true;
    }
};

#endif // EON_HAS_SERIALPORT

#include "MultimeterPlugin.moc"
