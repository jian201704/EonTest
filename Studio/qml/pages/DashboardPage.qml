import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// ============================================================
// DashboardPage — 多 CELL 并行看板（P1）
// 顶部：摘要指标卡片
// 中部：每个 CELL 的状态面板网格
// 底部：资源冲突视图 + 能力浏览器
// ============================================================
Rectangle {
    id: root
    color: "transparent"

    // 从后端获取的数据
    property int totalCells: backend ? backend.cellCount : 1
    property int healthyCells: backend ? backend.healthyCellCount : 0
    property int conflictCount: backend ? backend.resourceConflictCount : 0
    property int totalTasks: backend ? backend.taskListModel.rowCount() : 0
    property int runningTasks: backend ? backend.runningTaskCount : 0
    property int failedTasks: backend ? backend.failedTaskCount : 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // ============================================================
        // 标题行
        // ============================================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "📊 Parallel Dashboard"
                color: "#00d2ff"
                font.pixelSize: 18
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // 刷新按钮
            Button {
                text: "↻ Refresh"
                flat: true
                onClicked: backend.refreshDashboard()
            }
        }

        // ============================================================
        // 摘要指标卡片行
        // ============================================================
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            spacing: 8

            DashboardCard {
                title: "CELLs"
                value: totalCells.toString()
                sub: healthyCells + " healthy"
                accentColor: "#00d2ff"
            }
            DashboardCard {
                title: "Running"
                value: runningTasks.toString()
                sub: "tasks in progress"
                accentColor: "#4488ff"
            }
            DashboardCard {
                title: "Completed"
                value: (totalTasks - runningTasks - failedTasks).toString()
                sub: "tasks"
                accentColor: "#00c853"
            }
            DashboardCard {
                title: "Failed"
                value: failedTasks.toString()
                sub: "tasks"
                accentColor: failedTasks > 0 ? "#ff5252" : "#667788"
            }
            DashboardCard {
                title: "Conflicts"
                value: conflictCount.toString()
                sub: "resource"
                accentColor: conflictCount > 0 ? "#ffab00" : "#667788"
            }
        }

        // ============================================================
        // CELL 状态面板网格
        // ============================================================
        Text {
            text: "🖥️ Cell Status"
            color: "#e0e0e0"
            font.pixelSize: 14
            font.bold: true
        }

        GridLayout {
            id: cellGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: Math.min(4, totalCells)
            rowSpacing: 6
            columnSpacing: 6

            Repeater {
                model: totalCells
                delegate: CellDashboardPanel {
                    cellId: modelData
                    cellLabel: "CELL " + (modelData + 1)
                    cellStatus: backend ? backend.cellHealth(modelData) : "unknown"
                    completedSteps: backend ? backend.cellCompletedSteps(modelData) : 0
                    reportsDir: backend ? backend.cellReportsDir(modelData) : ""
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }

        // ============================================================
        // 底部：资源冲突 + 能力浏览器
        // ============================================================
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            spacing: 8

            // 资源冲突视图
            ResourceConflictView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                conflictCount: root.conflictCount
            }

            // 能力浏览器（插件清单）
            Rectangle {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                radius: 6
                color: "#0d1525"
                border.width: 1
                border.color: "#2a3a5e"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Text {
                        text: "🧩 Plugin Capabilities"
                        color: "#e0e0e0"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: backend ? backend.capabilityRegistryModel : null
                        clip: true

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 24
                            color: index % 2 === 0 ? "#16213e" : "#1a1a3e"
                            radius: 3

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 4
                                Text {
                                    text: model.pluginId
                                    color: "#00d2ff"
                                    font.pixelSize: 11
                                    Layout.preferredWidth: 100
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: model.version
                                    color: "#667788"
                                    font.pixelSize: 10
                                    Layout.preferredWidth: 50
                                }
                                Text {
                                    text: model.capabilities
                                    color: "#888888"
                                    font.pixelSize: 10
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
