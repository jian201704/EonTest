import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 表头单元格
Rectangle {
    property string text: ""
    property int w: 60
    property bool sorted: false

    Layout.preferredWidth: w
    Layout.fillHeight: true
    color: "transparent"

    Text {
        anchors.fill: parent
        anchors.margins: 4
        text: parent.text
        color: "#6688aa"
        font.pixelSize: 11
        font.bold: true
        verticalAlignment: Text.AlignVCenter
    }
}
