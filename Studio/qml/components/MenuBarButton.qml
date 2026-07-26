import QtQuick
import QtQuick.Controls

// 自定义菜单栏按钮 — 替代 MenuBar，兼容 Material Dark
Button {
    id: root
    flat: true
    font.pixelSize: 12
    property alias popup: menuPopup
    contentItem: Text {
        text: root.text
        color: root.hovered || menuPopup.opened ? "#ffffff" : "#8899aa"
        font.pixelSize: 12
        leftPadding: 10; rightPadding: 10
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        color: root.hovered || menuPopup.opened ? "#1a3050" : "transparent"
        border.width: root.hovered || menuPopup.opened ? 1 : 0
        border.color: "#3d6f98"
        radius: 3
    }
    Popup {
        id: menuPopup
        y: parent.height
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        padding: 0
        background: Rectangle {
            color: "#16213e"
            border.color: "#2a3a5e"
            border.width: 1
            radius: 4
        }
    }
}
