// Tool: Generate .xlsx test workflow files from existing .json definitions.
// Uses QXlsx. Run once to produce Workflows/*.test.xlsx.
// Usage: GenerateTestXlsx <workflows-dir> <plugins-dir>

#include "xlsxdocument.h"
#include "xlsxformat.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

// Map pluginId -> deviceType for Sheet1
static QString pluginToDeviceType(const QString& pluginId) {
    if (pluginId.contains("sample"))       return "Sample";
    if (pluginId.contains("power"))        return "电源";
    if (pluginId.contains("voltage") || pluginId.contains("measure")) return "万用表";
    if (pluginId.contains("csv") || pluginId.contains("reporter"))    return "Reporter";
    return pluginId;
}

// Map pluginId -> action for Sheet1
static QString pluginToAction(const QString& pluginId) {
    if (pluginId.contains("sample.activity")) return "execute";
    if (pluginId.contains("sample.analyzer")) return "analyze";
    if (pluginId.contains("sample.reporter")) return "report";
    if (pluginId.contains("power"))           return "set_volt";
    if (pluginId.contains("voltage"))         return "read_volt";
    return "execute";
}

static bool generateXlsx(const QString& jsonPath, const QString& xlsxPath, QString& error) {
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "Cannot open: " + jsonPath;
        return false;
    }
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "Invalid JSON: " + jsonPath;
        return false;
    }
    QJsonObject root = doc.object();
    QJsonArray steps = root.value("steps").toArray();
    if (steps.isEmpty()) {
        error = "No steps in: " + jsonPath;
        return false;
    }

    QXlsx::Document xlsx;

    // --- Sheet1: 测试工步表 ---
    xlsx.addSheet("测试工步表");
    xlsx.selectSheet("测试工步表");

    // Header row
    QStringList headers = {
        "工步号", "设备分类", "设备名称", "通道/端口号", "测试项名称",
        "动作指令", "配置参数1", "配置参数2", "配置参数3",
        "下限值", "上限值", "单位", "超时(ms)", "失败处理", "备注"
    };
    QXlsx::Format headerFmt;
    headerFmt.setFontBold(true);
    for (int c = 0; c < headers.size(); ++c)
        xlsx.write(1, c + 1, QVariant(headers[c]), headerFmt);

    // Data rows
    for (int i = 0; i < steps.size(); ++i) {
        QJsonObject s = steps[i].toObject();
        int row = i + 2;
        QString stepId = s.value("stepId").toString();
        QString pluginId = s.value("pluginId").toString();
        int maxRetries = s.value("maxRetries").toInt(0);
        int timeoutMs = s.value("timeoutMs").toInt(0);
        QString failPolicy = s.value("failurePolicy").toString("fail_fast");
        QString onSuccess = s.value("onSuccessStepId").toString();

        QString stepNo = stepId.section('.', -1);

        // 测试文件：硬件类步骤填入 VIRTUAL 端口，调试用
        QString c1, c2, c3;
        if (pluginId == "power.supply" || pluginId == "measure.voltage" ||
            pluginId == "serial.send" || pluginId == "serial.receive") {
            c1 = "VIRTUAL";
            c2 = "9600";
            if (pluginId == "power.supply") c3 = "5.0";
        }

        xlsx.write(row, 1, stepNo);                                        // A
        xlsx.write(row, 2, pluginToDeviceType(pluginId));                  // B
        xlsx.write(row, 3, pluginId);                                      // C
        xlsx.write(row, 4, QString());                                     // D
        xlsx.write(row, 5, stepId);                                        // E
        xlsx.write(row, 6, pluginToAction(pluginId));                      // F
        xlsx.write(row, 7, c1);                                            // G
        xlsx.write(row, 8, c2);                                            // H
        xlsx.write(row, 9, c3);                                            // I
        xlsx.write(row, 10, QString());                                    // J
        xlsx.write(row, 11, QString());                                    // K
        xlsx.write(row, 12, QString());                                    // L
        xlsx.write(row, 13, timeoutMs > 0 ? QString::number(timeoutMs) : QString()); // M
        xlsx.write(row, 14, failPolicy == "continue_on_error" ? "继续" : "停机"); // N
        xlsx.write(row, 15, onSuccess.isEmpty() ? QString() : ("-> " + onSuccess)); // O
    }

    // --- Sheet2: 设备资源映射 (15 列标准格式) ---
    xlsx.addSheet("设备资源映射");
    xlsx.selectSheet("设备资源映射");
    QStringList h2 = {"设备名称", "设备分类", "通道号", "连接方式", "地址",
                      "端口/波特率", "数据位", "校验位", "停止位", "流控",
                      "设备型号", "校准有效期", "驱动实现方式", "通道固有参数", "启用状态"};
    for (int c = 0; c < h2.size(); ++c)
        xlsx.write(1, c + 1, QVariant(h2[c]), headerFmt);
    xlsx.write(2, 1, "sample.activity");
    xlsx.write(2, 2, "Sample");
    xlsx.write(2, 4, "virtual");
    xlsx.write(2, 15, "启用");

    // --- Sheet3: 产品追溯信息 (placeholder) ---
    xlsx.addSheet("产品追溯信息");
    xlsx.selectSheet("产品追溯信息");
    QStringList h3 = {"字段名", "值"};
    for (int c = 0; c < h3.size(); ++c)
        xlsx.write(1, c + 1, QVariant(h3[c]), headerFmt);
    xlsx.write(2, 1, "产品型号");
    xlsx.write(2, 2, "EonTest-Demo");
    xlsx.write(3, 1, "用例版本号");
    xlsx.write(3, 2, "V1.0");

    // Remove default "Sheet1" if it exists
    QStringList sheets = xlsx.sheetNames();
    for (const auto& s : sheets) {
        if (s == "Sheet1") {
            xlsx.selectSheet("Sheet1");
            // Can't delete sheet easily, leave it
            break;
        }
    }

    return xlsx.saveAs(xlsxPath);
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    QString workflowsDir = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
    // Plugins dir not actually needed, placeholder kept for signature

    QDir dir(workflowsDir);
    QStringList jsonFiles = dir.entryList({"*.workflow.json"}, QDir::Files, QDir::Name);

    int ok = 0, fail = 0;
    for (const auto& jf : jsonFiles) {
        QString base = QFileInfo(jf).completeBaseName(); // e.g. "minimal.workflow"
        // Remove .workflow suffix if present
        if (base.endsWith(".workflow"))
            base = base.left(base.size() - 9);
        QString outPath = dir.filePath(base + ".test.xlsx");

        QString error;
        if (generateXlsx(dir.filePath(jf), outPath, error)) {
            qInfo().noquote() << QString("OK: %1 → %2").arg(jf, outPath);
            ok++;
        } else {
            qInfo().noquote() << QString("FAIL: %1 - %2").arg(jf, error);
            fail++;
        }
    }

    qInfo().noquote() << QString("Generated %d xlsx files, %d failed.").arg(ok).arg(fail);
    return fail > 0 ? 1 : 0;
}
