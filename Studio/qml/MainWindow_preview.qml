import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "pages"

// 这个文件仅用于 Qt Creator Design 模式预览
// 将 ApplicationWindow 换成了 Item，避免 M208 错误
Item {
    width: 1360
    height: 820

    // 模拟 MainWindow 的内容
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
    }
}
