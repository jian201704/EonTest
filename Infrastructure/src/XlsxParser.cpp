#include "eon/infra/XlsxParser.h"
#include "eon/domain/WorkflowDefinition.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QVariant>

#include "xlsxdocument.h"

namespace eon::infra {

namespace {

QString cellStr(QXlsx::Document& doc, int row, int col, const QString& def = {}) {
    QVariant v = doc.read(row, col);
    if (v.isNull() || !v.isValid()) return def;
    return v.toString().trimmed();
}

int cellInt(QXlsx::Document& doc, int row, int col, int def = 0) {
    QVariant v = doc.read(row, col);
    if (v.isNull() || !v.isValid()) return def;
    bool ok = false;
    int r = v.toInt(&ok);
    return ok ? r : def;
}

void parseFailurePolicyText(const QString& text, int* outMaxRetries, QString* outFailurePolicy) {
    QString t = text.trimmed();
    if (t.contains("停机") || t.contains("fail_fast")) {
        *outMaxRetries = 0; *outFailurePolicy = "fail_fast";
    } else if (t.contains("继续") || t.contains("continue")) {
        *outMaxRetries = 0; *outFailurePolicy = "continue_on_error";
    } else if (t.contains("重试") || t.contains("retry")) {
        static QRegularExpression re(R"((\d+))");
        auto m = re.match(t);
        *outMaxRetries = m.hasMatch() ? m.captured(1).toInt() : 1;
        *outFailurePolicy = "fail_fast";
    } else {
        *outMaxRetries = 0; *outFailurePolicy = "fail_fast";
    }
}

QString inferPluginId(const QString& deviceType, const QString& action) {
    QString t = deviceType.trimmed();
    QString a = action.trimmed();
    if (t.contains("电源") || t.contains("Power") || t.contains("power")) return "power.supply";
    if (t.contains("万用表") || t.contains("Multimeter") || t.contains("voltage") || t.contains("Voltage")) return "measure.voltage";
    if (t.contains("示波器") || t.contains("Oscilloscope")) return "oscilloscope.measure";
    if (t.contains("RF") || t.contains("射频") || t.contains("频谱")) return "rf.analyzer";
    if (t.contains("CAN")) return "can.send";
    if (t.contains("DoIP", Qt::CaseInsensitive) || t.contains("诊断以太网") ||
        a.contains("doip", Qt::CaseInsensitive)) return "doip.master";
    if (t.contains("ModBus") || t.contains("Modbus") || t.contains("modbus")) return "modbus.master";
    if (t.contains("Python") || t.contains("python")) return "python.script";
    if (t.contains("IO") || t.contains("板卡") || t.contains("GPIO")) return "gpio.set";
    if (t.contains("串口") || t.contains("Serial") || t.contains("serial"))
        return a.contains("read") ? "serial.receive" : "serial.send";
    if (t.contains("Sample") || t.contains("sample")) return "sample.activity";
    if (a.contains("delay") || a.contains("wait") || a.contains("延时") || a.contains("等待"))
        return "delay";
    if (t.contains("SCPI") || t.contains("scpi") || t.contains("通用") || t.contains("VISA"))
        return "scpi.command";
    return deviceType.trimmed();
}

/// 解析 key=value 格式的配置字符串，如 "func=3,addr=0,count=4,transport=tcp"
static void parseConfigPairs(const QString& text, QVariantMap& params) {
    QStringList pairs;
    QString current;
    int nesting = 0;
    QChar quote;
    for (const QChar ch : text) {
        if (quote.isNull() && (ch == '\"' || ch == '\'')) quote = ch;
        else if (!quote.isNull() && ch == quote) quote = {};
        else if (quote.isNull() && (ch == '[' || ch == '{')) ++nesting;
        else if (quote.isNull() && (ch == ']' || ch == '}')) nesting = qMax(0, nesting - 1);
        if (ch == ',' && quote.isNull() && nesting == 0) {
            if (!current.trimmed().isEmpty()) pairs.append(current);
            current.clear();
        } else {
            current.append(ch);
        }
    }
    if (!current.trimmed().isEmpty()) pairs.append(current);
    for (const auto& pair : pairs) {
        const int eq = pair.indexOf('=');
        if (eq <= 0) continue;
        const QString key = pair.left(eq).trimmed();
        const QString val = pair.mid(eq + 1).trimmed();
        if (key.isEmpty()) continue;
        if ((val.startsWith('[') && val.endsWith(']')) ||
            (val.startsWith('{') && val.endsWith('}'))) {
            const auto document = QJsonDocument::fromJson(val.toUtf8());
            if (!document.isNull()) {
                params[key] = document.toVariant();
                continue;
            }
        }
        // 尝试自动转数字
        bool ok = false;
        int ival = val.toInt(&ok);
        if (ok) { params[key] = ival; continue; }
        double dval = val.toDouble(&ok);
        if (ok) { params[key] = dval; continue; }
        params[key] = val;
    }
}

bool selectSheetByName(QXlsx::Document& doc, const QStringList& names, int fallbackIdx) {
    for (const auto& n : names)
        if (doc.selectSheet(n)) return true;
    QStringList avail = doc.sheetNames();
    if (fallbackIdx >= 1 && fallbackIdx <= avail.size())
        return doc.selectSheet(avail[fallbackIdx - 1]);
    return false;
}

/// Sheet2: 设备通道完整映射表 — 动态列检测 + 全部通信参数
QMap<QString, QVariantMap> parseDeviceChannel(QXlsx::Document& doc) {
    QMap<QString, QVariantMap> map;
    int lastRow = doc.dimension().lastRow();
    if (lastRow < 2) return map;
    int maxCol = qMin(doc.dimension().lastColumn(), 20);

    int hdrRow = -1;
    for (int r = 1; r <= qMin(lastRow, 30); ++r) {
        QStringList rowHeaders;
        for (int c = 1; c <= qMin(maxCol, 10); ++c)
            rowHeaders << cellStr(doc, r, c);
        // 检查是否包含典型的表头关键词
        bool hasDevName = false, hasChan = false;
        for (const auto& h : rowHeaders) {
            if (h.contains("设备名称") || h.contains("名称")) hasDevName = true;
            if (h.contains("通道号") || h.contains("通道")) hasChan = true;
        }
        if (hasDevName && hasChan) {
            // 确认至少还有 连接方式/主地址/波特率/启用 之一
            bool hasConnOrAddr = false;
            for (const auto& h : rowHeaders) {
                if (h.contains("连接") || h.contains("主地址") || h.contains("端口") ||
                    h.contains("波特") || h.contains("启用") || h.contains("状态")) {
                    hasConnOrAddr = true; break;
                }
            }
            if (hasConnOrAddr) { hdrRow = r; break; }
        }
    }
    if (hdrRow < 0) return map;

    struct ColMap { int col; QString key; };
    QList<ColMap> colMaps;
    for (int c = 1; c <= maxCol; ++c) {
        QString h = cellStr(doc, hdrRow, c);
        if (h.contains("设备名称") || h.contains("名称"))
            colMaps.append({c, "deviceName"});
        else if (h.contains("设备分类") || h.contains("分类"))
            colMaps.append({c, "deviceCategory"});
        else if (h.contains("通道号") || (h.contains("通道") && !h.contains("固有") && !h.contains("完整")))
            colMaps.append({c, "channelId"});
        else if (h.contains("别名"))
            colMaps.append({c, "channelAlias"});
        else if (h.contains("连接方式") || h.contains("连接"))
            colMaps.append({c, "connType"});
        else if (h.contains("主地址"))
            colMaps.append({c, "address"});
        else if (h.contains("子地址") || h.contains("子端口") || h.contains("从地址"))
            colMaps.append({c, "subAddress"});
        else if (h.contains("端口") && (h.contains("波特率") || h.contains("波特") || h.contains("率")))
            colMaps.append({c, "baudRate"});
        else if (h.contains("地址") || h == "端口")
            colMaps.append({c, "address"});
        else if (h.contains("设备型号") || h.contains("型号"))
            colMaps.append({c, "model"});
        else if (h.contains("校准有效期") || h.contains("校准"))
            colMaps.append({c, "calibration"});
        else if (h.contains("驱动实现") || h.contains("驱动方式"))
            colMaps.append({c, "driverImpl"});
        else if (h.contains("波特率"))
            colMaps.append({c, "baudRate"});
        else if (h.contains("数据位"))
            colMaps.append({c, "dataBits"});
        else if (h.contains("校验位") || h.contains("奇偶"))
            colMaps.append({c, "parity"});
        else if (h.contains("停止位"))
            colMaps.append({c, "stopBits"});
        else if (h.contains("流控"))
            colMaps.append({c, "flowCtrl"});
        else if (h.contains("固有参数") || h.contains("参数"))
            colMaps.append({c, "channelParams"});
        else if (h.contains("启用") || h.contains("状态"))
            colMaps.append({c, "enabled"});
    }

    for (int r = hdrRow + 1; r <= lastRow; ++r) {
        QString name;
        QVariantMap dev;
        for (const auto& cm : colMaps) {
            QString val = cellStr(doc, r, cm.col);
            if (val.isEmpty() || val == "-") continue;
            if (cm.key == "deviceName") name = val;
            dev[cm.key] = val;
        }
        if (!name.isEmpty() && !dev.isEmpty())
            map[name] = dev;
    }
    return map;
}

/// Sheet3: 产品追溯信息表 (key-value)
QVariantMap parseProductTrace(QXlsx::Document& doc) {
    QVariantMap info;
    int lastRow = doc.dimension().lastRow();
    int startRow = 2;
    for (int r = 1; r <= qMin(lastRow, 20); ++r) {
        QString c1 = cellStr(doc, r, 1);
        if (c1.contains("字段") || c1.contains("产品型号") || c1.contains("产品")) {
            startRow = r + 1; break;
        }
    }
    for (int r = startRow; r <= lastRow; ++r) {
        QString key = cellStr(doc, r, 1);
        QString val = cellStr(doc, r, 2);
        if (!key.isEmpty() && !key.contains("字段") && !key.contains("说明"))
            info[key] = val;
    }
    return info;
}

} // anonymous namespace

// ============================================================================
// 公有 API
// ============================================================================

bool parseWorkflowDefinitionXlsx(
    const QString& filePath,
    eon::domain::WorkflowDefinition* workflowDefinition,
    QString* errorMessage)
{
    if (workflowDefinition == nullptr) {
        if (errorMessage) *errorMessage = "WorkflowDefinition output is null.";
        return false;
    }

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        if (errorMessage) *errorMessage = QString("File not found: %1").arg(filePath);
        return false;
    }

    QXlsx::Document doc(filePath);
    if (!doc.load()) {
        if (errorMessage) *errorMessage = QString("Cannot open .xlsx: %1").arg(filePath);
        return false;
    }

    eon::domain::WorkflowDefinition wf;
    wf.workflowId = fi.completeBaseName();

    // --- Step 1: Sheet2 — 设备通道完整映射表 ---
    QMap<QString, QVariantMap> deviceMap;
    if (selectSheetByName(doc, {"DeviceChannel", "设备通道完整映射表", "设备资源映射"}, 2))
        deviceMap = parseDeviceChannel(doc);

    // --- Step 2: Sheet1 — 测试工步表 ---
    if (!selectSheetByName(doc, {"TestStep", "测试工步表", "TestStep 测试工步表"}, 1)) {
        if (errorMessage) *errorMessage = "Sheet1 (TestStep) not found.";
        return false;
    }

    int lastRow = doc.dimension().lastRow();
    int dataStart = 2;
    int headerRow = 1;
    for (int r = 1; r <= qMin(lastRow, 20); ++r) {
        if (cellStr(doc, r, 1).contains("工步号") || cellStr(doc, r, 1).contains("Step")) {
            headerRow = r;
            dataStart = r + 1;
            break;
        }
    }
    if (dataStart > lastRow) {
        if (errorMessage) *errorMessage = "Sheet1 has no data rows.";
        return false;
    }

    int decodeConfigColumn = -1;
    for (int col = 1; col <= doc.dimension().lastColumn(); ++col) {
        const QString header = cellStr(doc, headerRow, col).toLower();
        if (header.contains("解码配置") || header.contains("decodeconfig") ||
            header.contains("decode profile") || header.contains("decodeprofile")) {
            decodeConfigColumn = col;
            break;
        }
    }

    for (int row = dataStart; row <= lastRow; ++row) {
        QString stepNo   = cellStr(doc, row, 1);
        QString devType  = cellStr(doc, row, 2);
        QString devName  = cellStr(doc, row, 3);
        QString ch       = cellStr(doc, row, 4);
        QString testItem = cellStr(doc, row, 5);
        QString action   = cellStr(doc, row, 6);
        QString c1       = cellStr(doc, row, 7);
        QString c2       = cellStr(doc, row, 8);
        QString c3       = cellStr(doc, row, 9);
        QString low      = cellStr(doc, row, 10);
        QString high     = cellStr(doc, row, 11);
        QString unit     = cellStr(doc, row, 12);
        int timeout      = cellInt(doc, row, 13);
        QString failPol  = cellStr(doc, row, 14);
        QString remark   = cellStr(doc, row, 15);
        const QString decodeConfig = decodeConfigColumn > 0
            ? cellStr(doc, row, decodeConfigColumn) : QString();

        if (devType.isEmpty() && action.isEmpty()) continue;

        eon::domain::ActivityStep step;
        step.stepId = QString("step.%1").arg(
            stepNo.isEmpty() ? QString::number(row - dataStart + 1) : stepNo);
        step.pluginId = inferPluginId(devType, action);

        int maxRet = 0; QString failAct;
        parseFailurePolicyText(failPol, &maxRet, &failAct);
        step.policy.maxRetries = maxRet;
        step.policy.timeoutMs = timeout;
        step.policy.failurePolicy = (failAct == "continue_on_error")
            ? eon::domain::FailurePolicy::ContinueOnError
            : eon::domain::FailurePolicy::FailFast;

        // 基本步骤参数
        QVariantMap params;
        if (!devName.isEmpty())  params["deviceName"] = devName;
        if (!ch.isEmpty())       params["channel"]    = ch;
        if (!testItem.isEmpty()) params["testItem"]   = testItem;
        if (!action.isEmpty())   params["action"]     = action;
        if (!devType.isEmpty())  params["deviceType"] = devType;
        if (!c1.isEmpty()) params["config1"] = c1;
        if (!c2.isEmpty()) params["config2"] = c2;
        if (!c3.isEmpty()) params["config3"] = c3;
        if (!low.isEmpty())  params["lowerLimit"] = low;
        if (!high.isEmpty()) params["upperLimit"] = high;
        if (!unit.isEmpty()) params["unit"] = unit;
        if (!remark.isEmpty()) params["remark"] = remark;
        if (!decodeConfig.isEmpty()) {
            const auto document = QJsonDocument::fromJson(decodeConfig.toUtf8());
            if (!document.isNull()) {
                params[document.isArray() ? "decodeSpecs" : "decodeProfile"] = document.toVariant();
            } else {
                parseConfigPairs(decodeConfig, params);
            }
        }

        // 解析 config1/2/3 中的 key=value 对（如 "func=3,addr=0,count=4"）
        if (!c1.isEmpty()) parseConfigPairs(c1, params);
        if (!c2.isEmpty()) parseConfigPairs(c2, params);
        if (!c3.isEmpty()) parseConfigPairs(c3, params);

        // scpi.command 专用映射：动作列 = SCPI 指令, config1~3 = saveAs/expect/timeout
        if (step.pluginId == "scpi.command") {
            if (!action.isEmpty()) params["command"] = action;
            if (!c1.isEmpty())     params["saveAs"]  = c1;
            if (!c2.isEmpty())     params["expect"]  = c2;
            if (!c3.isEmpty())     params["timeoutMs"] = c3.toInt();
        }

        // --- Sheet2 回填：通信参数 ---
        if (!devName.isEmpty() && deviceMap.contains(devName)) {
            const auto& d = deviceMap[devName];
            QString addr    = d.value("address").toString();
            QString subAddr = d.value("subAddress").toString();
            QString conn    = d.value("connType").toString().trimmed();
            QString baudStr = d.value("baudRate").toString();
            QString parity  = d.value("parity").toString();
            QString dataStr = d.value("dataBits").toString();
            QString stopStr = d.value("stopBits").toString();

            QString portVal;
            if (!addr.isEmpty())         portVal = addr;
            else if (!subAddr.isEmpty()) portVal = subAddr;
            else                         portVal = devName;

            // 判断连接类型
            QString connLower = conn.toLower();
            if (connLower.contains("visa") || connLower.contains("gpib") || connLower.contains("usb")) {
                params["connectType"] = "visa";
                if (!addr.isEmpty()) params["resource"] = addr;
            } else if (connLower.contains("tcp") || connLower.contains("lan") || connLower.contains("以太网") || connLower.contains("socket")) {
                params["connectType"] = "tcp";
                if (!addr.isEmpty()) params["host"] = addr;
                if (!subAddr.isEmpty()) params["tcpPort"] = subAddr.toInt();
                // 如果有 port 列是数字，当 tcpPort
                QString portStr = d.value("port", d.value("tcpPort", "")).toString();
                if (!portStr.isEmpty()) params["tcpPort"] = portStr.toInt();
            } else if (connLower.contains("serial") || connLower.contains("串口") || connLower.contains("com") || connLower.contains("rs232")) {
                params["connectType"] = "serial";
            } else {
                // 自动检测：地址是 COMx → serial，IP → tcp
                if (portVal.startsWith("COM", Qt::CaseInsensitive) || portVal.startsWith("/dev/tty"))
                    params["connectType"] = "serial";
                else if (portVal.contains(".") || portVal.contains("::"))
                    params["connectType"] = "tcp";
            }

            // 不要覆盖工步配置中的 port。TCP 设备的地址通常是 IP，旧逻辑
            // 将 port 直接写成 portVal，导致 DoIP 的 13400 被覆盖为
            // "172.16.0.8"，最终 toUInt() 得到端口 0。
            if (!params.contains("port"))
                params["port"] = portVal;
            // 万用表插件用 dmmPort
            if (step.pluginId == "measure.voltage" || step.pluginId.contains("multimeter"))
                params["dmmPort"] = portVal;

            // 如果地址是 Virtual，启用虚拟模式跳过硬件
            if (addr.contains("Virtual", Qt::CaseInsensitive) || addr.contains("virtual"))
                params["virtualMode"] = true;

            if (!conn.isEmpty())    params["backend"]  = conn;
            if (!baudStr.isEmpty()) params["baudRate"] = baudStr.toInt();
            if (!parity.isEmpty())  params["parity"]   = parity;
            if (!dataStr.isEmpty()) params["dataBits"] = dataStr.toInt();
            if (!stopStr.isEmpty()) params["stopBits"] = stopStr;

            // Modbus 专用参数映射
            if (step.pluginId == "modbus.master") {
                if (params.contains("connectType")) {
                    QString ct = params["connectType"].toString();
                    if (ct == "tcp") params["transport"] = "tcp";
                    else if (ct == "serial") params["transport"] = "serial";
                }
                if (params.contains("tcpPort"))
                    params["port"] = params["tcpPort"];
                if (params.contains("address"))
                    params["slaveId"] = params["address"].toInt();
            }
        }

        step.initialData = params;

        // 默认顺序执行 — 用下一行的工步号，不用序号
        if (row < lastRow) {
            QString nextStepNo = cellStr(doc, row + 1, 1);
            if (!nextStepNo.isEmpty())
                step.onSuccessStepId = QString("step.%1").arg(nextStepNo);
        }

        wf.steps.append(step);
    }

    if (wf.steps.isEmpty()) {
        if (errorMessage) *errorMessage = "No steps found in Sheet1.";
        return false;
    }
    wf.entryStepId = wf.steps.first().stepId;

    // --- Step 3: Sheet3 — 产品追溯信息表 ---
    if (selectSheetByName(doc, {"ProductTrace", "产品追溯信息表", "产品追溯信息"}, 3)) {
        QVariantMap info = parseProductTrace(doc);
        if (!info.isEmpty())
            wf.initialData["productInfo"] = info;
    }

    QVariantList instrList;
    for (auto it = deviceMap.constBegin(); it != deviceMap.constEnd(); ++it)
        instrList.append(it.value());
    if (!instrList.isEmpty())
        wf.initialData["instruments"] = instrList;

    *workflowDefinition = wf;
    return true;
}

// ============================================================================
// writeWorkflowDefinitionXlsx — 将 WorkflowDefinition 写入三 Sheet .xlsx
// ============================================================================
bool writeWorkflowDefinitionXlsx(
    const QString& filePath,
    const eon::domain::WorkflowDefinition& wf,
    QString* errorMessage)
{
    using namespace eon::domain;

    QXlsx::Document xlsx;
    QXlsx::Format headerFmt;
    headerFmt.setFontBold(true);

    // --- Sheet1: 测试工步表 ---
    xlsx.addSheet("测试工步表");
    xlsx.selectSheet("测试工步表");

    QStringList headers = {
        "工步号", "设备分类", "设备名称", "通道/端口号", "测试项名称",
        "动作指令", "配置参数1", "配置参数2", "配置参数3",
        "下限值", "上限值", "单位", "超时(ms)", "失败处理", "备注"
    };
    for (int c = 0; c < headers.size(); ++c)
        xlsx.write(1, c + 1, QVariant(headers[c]), headerFmt);

    for (int i = 0; i < wf.steps.size(); ++i) {
        const auto& s = wf.steps[i];
        int row = i + 2;
        QString stepNo = s.stepId.section('.', -1);

        xlsx.write(row, 1, stepNo);                                              // A 工步号
        xlsx.write(row, 2, s.pluginId.section('.', 0, 0));                       // B 设备分类
        xlsx.write(row, 3, s.pluginId);                                          // C 设备名称
        xlsx.write(row, 4, s.initialData.value("channel").toString());           // D 通道
        xlsx.write(row, 5, s.initialData.value("testItem").toString());          // E 测试项
        xlsx.write(row, 6, [&]() { auto v = s.initialData.value("action"); return v.isValid() ? v.toString() : QString("execute"); }());   // F 动作指令
        xlsx.write(row, 7, s.initialData.value("config1").toString());           // G
        xlsx.write(row, 8, s.initialData.value("config2").toString());           // H
        xlsx.write(row, 9, s.initialData.value("config3").toString());           // I
        xlsx.write(row, 10, QVariant());                                         // J 下限
        xlsx.write(row, 11, QVariant());                                         // K 上限
        xlsx.write(row, 12, QVariant());                                         // L 单位
        xlsx.write(row, 13, s.policy.timeoutMs > 0
            ? QString::number(s.policy.timeoutMs) : QString());                  // M 超时
        xlsx.write(row, 14,                                                     // N 失败处理
            s.policy.failurePolicy == FailurePolicy::ContinueOnError ? "继续" : "停机");
        xlsx.write(row, 15, s.onSuccessStepId.isEmpty()
            ? QString() : ("-> " + s.onSuccessStepId));                          // O 备注
    }

    // --- Sheet2: 设备资源映射 (15 列标准格式) ---
    xlsx.addSheet("设备资源映射");
    xlsx.selectSheet("设备资源映射");
    QStringList h2 = {"设备名称", "设备分类", "通道号", "连接方式", "地址",
                      "端口/波特率", "数据位", "校验位", "停止位", "流控",
                      "设备型号", "校准有效期", "驱动实现方式", "通道固有参数", "启用状态"};
    for (int c = 0; c < h2.size(); ++c)
        xlsx.write(1, c + 1, QVariant(h2[c]), headerFmt);

    QVariantList instruments = wf.initialData.value("instruments").toList();
    for (int i = 0; i < instruments.size(); ++i) {
        QVariantMap im = instruments[i].toMap();
        int row = i + 2;
        xlsx.write(row, 1,  im.value("deviceName").toString());       // 设备名称
        xlsx.write(row, 2,  im.value("deviceCategory").toString());    // 设备分类
        xlsx.write(row, 3,  im.value("channelId").toString());         // 通道号
        xlsx.write(row, 4,  im.value("connType").toString());          // 连接方式
        xlsx.write(row, 5,  im.value("address").toString());           // 地址
        xlsx.write(row, 6,  im.value("baudRate").toString());          // 端口/波特率
        xlsx.write(row, 7,  im.value("dataBits").toString());          // 数据位
        xlsx.write(row, 8,  im.value("parity").toString());            // 校验位
        xlsx.write(row, 9,  im.value("stopBits").toString());          // 停止位
        xlsx.write(row, 10, im.value("flowCtrl").toString());          // 流控
        xlsx.write(row, 11, im.value("model").toString());             // 设备型号
        xlsx.write(row, 12, im.value("calibration").toString());       // 校准有效期
        xlsx.write(row, 13, im.value("driverImpl").toString());        // 驱动实现方式
        xlsx.write(row, 14, im.value("channelParams").toString());     // 通道固有参数
        xlsx.write(row, 15, im.value("enabled", true).toBool() ? "启用" : "禁用"); // 启用状态
    }
    if (instruments.isEmpty()) {
        xlsx.write(2, 1,  wf.workflowId);
        xlsx.write(2, 2,  "Virtual");
        xlsx.write(2, 4,  "virtual");
        xlsx.write(2, 15, "启用");
    }

    // --- Sheet3: 产品追溯信息 ---
    xlsx.addSheet("产品追溯信息");
    xlsx.selectSheet("产品追溯信息");
    QStringList h3 = {"字段名", "值"};
    for (int c = 0; c < h3.size(); ++c)
        xlsx.write(1, c + 1, QVariant(h3[c]), headerFmt);

    QVariantMap productInfo = wf.initialData.value("productInfo").toMap();
    int piRow = 2;
    for (auto it = productInfo.constBegin(); it != productInfo.constEnd(); ++it) {
        xlsx.write(piRow, 1, it.key());
        xlsx.write(piRow, 2, it.value().toString());
        ++piRow;
    }
    if (productInfo.isEmpty()) {
        xlsx.write(2, 1, "产品型号");
        xlsx.write(2, 2, wf.workflowId);
        xlsx.write(3, 1, "用例版本号");
        xlsx.write(3, 2, "V1.0");
    }

    if (!xlsx.saveAs(filePath)) {
        if (errorMessage)
            *errorMessage = QString("Failed to save xlsx: %1").arg(filePath);
        return false;
    }
    return true;
}

} // namespace eon::infra
