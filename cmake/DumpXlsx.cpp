// Quick tool: dump .xlsx sheet names + first 3 rows of each sheet
#include "xlsxdocument.h"
#include <QDebug>
#include <QFileInfo>

int main(int argc, char* argv[]) {
    if (argc < 2) { qWarning() << "Usage: DumpXlsx <file.xlsx>"; return 1; }
    QString path = QString::fromLocal8Bit(argv[1]);
    QXlsx::Document doc(path);
    if (!doc.load()) { qWarning() << "Cannot open" << path; return 1; }

    qInfo() << "File:" << QFileInfo(path).fileName();
    qInfo() << "Sheets:" << doc.sheetNames();

    for (const auto& name : doc.sheetNames()) {
        doc.selectSheet(name);
        int lastRow = qMin(doc.dimension().lastRow(), 5); // first 5 rows
        int lastCol = doc.dimension().lastColumn();
        qInfo() << "\n=== Sheet:" << name << "===";
        qInfo() << "  Dimensions:" << doc.dimension().lastRow() << "rows x" << lastCol << "cols";
        for (int r = 1; r <= lastRow; ++r) {
            QStringList rowVals;
            for (int c = 1; c <= lastCol; ++c) {
                QVariant v = doc.read(r, c);
                rowVals << (v.isNull() ? QString() : v.toString());
            }
            qInfo().noquote() << QString("  R%1:").arg(r) << rowVals;
        }
    }
    return 0;
}
