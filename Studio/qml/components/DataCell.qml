import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 数据单元格
Rectangle {
    property string text: ""
    property int w: 60
    property color textColor: "#c0c0c0"
    property bool elide: false

    Layout.preferredWidth: w
    Layout.fillHeight: true
    color: "transparent"

    Text {
        anchors.fill: parent
        anchors.margins: 4
        text: parent.text
        color: textColor
        font.pixelSize: 11
        verticalAlignment: Text.AlignVCenter
        elide: parent.elide ? Text.ElideRight : Text.ElideNone
    }
}
