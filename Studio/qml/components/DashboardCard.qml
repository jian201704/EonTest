import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 仪表盘卡片组件
Rectangle {
    property string title: ""
    property string value: "0"
    property string sub: ""
    property color accentColor: "#2196f3"

    Layout.fillWidth: true
    Layout.fillHeight: true
    radius: 6
    color: "#16213e"
    border.width: 1
    border.color: accentColor

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 2

        Text {
            text: title
            color: "#667788"
            font.pixelSize: 11
            Layout.alignment: Qt.AlignHCenter
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4
            Text {
                text: value
                color: "#ffffff"
                font.pixelSize: 20
                font.bold: true
            }
            Text {
                text: sub
                color: accentColor
                font.pixelSize: 16
                visible: sub !== ""
            }
        }
    }
}
