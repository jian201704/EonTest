import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 菜单行 item — 用于 Popup 内
ItemDelegate {
    id: root
    highlighted: hovered
    contentItem: Text {
        text: root.checkable ? (root.checked ? "✓ " : "  ") + root.text : root.text
        color: root.hovered ? "#ffffff" : "#c0c0c0"
        font.pixelSize: 12
        leftPadding: 12; rightPadding: 12
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        color: root.hovered ? "#1a3050" : "transparent"
    }
}
