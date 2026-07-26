import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ============================================================
// CellDashboardPanel — 单个 CELL 的仪表盘面板（P1 并行看板）
// 显示：CELL ID、健康状态、已执行步骤数、报告目录
// ============================================================
Rectangle {
    id: root

    property int cellId: 0
    property string cellLabel: "CELL " + (cellId + 1)
    property string cellStatus: "idle"     // idle/running/dead/unknown
    property int completedSteps: 0
    property string reportsDir: ""

    radius: 6
    color: "#0d1525"
    border.width: 1
    border.color: cellStatus === "dead" ? "#ff5252"
                 : cellStatus === "running" ? "#4488ff"
                 : "#2a3a5e"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        // 标题行：状态灯 + CELL 名
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Rectangle {
                width: 12; height: 12
                radius: 6
                color: cellStatus === "running" ? "#4488ff"
                     : cellStatus === "dead" ? "#ff5252"
                     : cellStatus === "idle" ? "#00c853"
                     : "#455a64"
            }

            Text {
                text: cellLabel
                color: "#e0e0e0"
                font.pixelSize: 13
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: cellStatus
                color: cellStatus === "running" ? "#4488ff"
                     : cellStatus === "dead" ? "#ff5252"
                     : cellStatus === "idle" ? "#00c853"
                     : "#888888"
                font.pixelSize: 10
                font.bold: true
            }
        }

        // 指标行
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                spacing: 1
                Text {
                    text: completedSteps.toString()
                    color: "#ffffff"
                    font.pixelSize: 22
                    font.bold: true
                }
                Text {
                    text: "steps done"
                    color: "#667788"
                    font.pixelSize: 9
                }
            }

            ColumnLayout {
                spacing: 1
                Text {
                    text: "⚡"
                    color: "#00d2ff"
                    font.pixelSize: 18
                }
                Text {
                    text: "active"
                    color: "#667788"
                    font.pixelSize: 9
                }
            }
        }

        // 报告目录链接
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            radius: 3
            color: "#1f2a44"
            visible: reportsDir !== ""

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4
                Text {
                    text: "📁"
                    font.pixelSize: 10
                }
                Text {
                    text: reportsDir
                    color: "#667788"
                    font.pixelSize: 9
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }
    }
}
