import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 状态徽章
Rectangle {
    property string status: "pending"
    property int w: 80

    Layout.preferredWidth: w
    Layout.fillHeight: true
    radius: 3

    color: status === "succeeded" ? "#0a3a0a"
         : status === "failed" ? "#3a0a0a"
         : status === "running" ? "#0a1a4a"
         : status === "skipped" ? "#3a3a0a"
         : "#1a1a1a"

    Text {
        anchors.centerIn: parent
        text: status
        color: status === "succeeded" ? "#00c853"
             : status === "failed" ? "#ff5252"
             : status === "running" ? "#4488ff"
             : status === "skipped" ? "#ffab00"
             : "#888888"
        font.pixelSize: 11
        font.bold: true
    }
}
