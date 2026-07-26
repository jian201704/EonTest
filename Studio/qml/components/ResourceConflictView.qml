import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ============================================================
// ResourceConflictView — 资源冲突可视化（P1）
// 显示当前持有资源的 CELL 列表，标记冲突
// ============================================================
Rectangle {
    id: root

    property int conflictCount: 0
    property var resourceHoldings: backend ? backend.resourceHoldings : null

    radius: 6
    color: "#0d1525"
    border.width: 1
    border.color: conflictCount > 0 ? "#ffab00" : "#2a3a5e"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        // 标题行
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "🔒 Resource Holdings"
                color: "#e0e0e0"
                font.pixelSize: 12
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // 冲突警告
            Rectangle {
                visible: conflictCount > 0
                height: 20
                radius: 3
                color: "#3a2a00"
                border.width: 1
                border.color: "#ffab00"
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 3
                    Text {
                        text: "⚠"
                        font.pixelSize: 10
                    }
                    Text {
                        text: conflictCount + " conflict(s)"
                        color: "#ffab00"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }
        }

        // 资源列表
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: backend ? backend.resourceHoldingModel : null
            clip: true
            spacing: 2

            delegate: Rectangle {
                width: ListView.view.width
                height: 24
                radius: 3
                color: model.conflict === true ? "#2a1a00" : "#16213e"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 6

                    // 资源名
                    Text {
                        text: model.resourceId
                        color: "#e0e0e0"
                        font.pixelSize: 11
                        font.bold: true
                        Layout.preferredWidth: 120
                        elide: Text.ElideRight
                    }

                    // 箭头
                    Text {
                        text: "→"
                        color: "#667788"
                        font.pixelSize: 11
                    }

                    // 持有者 CELL
                    Text {
                        text: "CELL " + (model.ownerCell + 1)
                        color: model.conflict === true ? "#ffab00" : "#00d2ff"
                        font.pixelSize: 11
                        Layout.preferredWidth: 60
                    }

                    // 模式
                    Text {
                        text: "(" + model.mode + ")"
                        color: "#667788"
                        font.pixelSize: 9
                    }

                    Item { Layout.fillWidth: true }

                    // 冲突标记
                    Rectangle {
                        visible: model.conflict === true
                        width: 16; height: 16
                        radius: 3
                        color: "#ffab00"
                        Text {
                            anchors.centerIn: parent
                            text: "!"
                            color: "#000000"
                            font.bold: true
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
    }
}
