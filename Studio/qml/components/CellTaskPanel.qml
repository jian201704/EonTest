import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ============================================================
// CellTaskPanel — 单个 CELL 的任务面板
// 用于多 CELL 网格布局
// ============================================================
Rectangle {
    id: root

    property int cellId: 0
    property var taskModel: null         // TaskListModel (全局)
    property string cellLabel: "CELL " + (cellId + 1)
    property int filteredCount: 0

    Layout.fillWidth: true
    Layout.fillHeight: true
    radius: 6
    color: "#0d1525"
    border.width: 1
    border.color: "#2a3a5e"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 2

        // CELL 标题栏
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            radius: 4
            color: "#16213e"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6

                Text {
                    text: "🖥️"
                    font.pixelSize: 12
                }
                Text {
                    text: cellLabel
                    color: "#00d2ff"
                    font.pixelSize: 12
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                // 该 CELL 的任务计数
                Text {
                    id: taskCountText
                    text: "0 tasks"
                    color: "#667788"
                    font.pixelSize: 10
                }

                // CELL 状态指示器
                Rectangle {
                    id: cellStatusDot
                    width: 10; height: 10
                    radius: 5
                    color: "#455a64"  // idle
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            radius: 3
            color: "#1f2a44"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                spacing: 4
                Text { text: "Test"; color: "#9ab0c8"; font.pixelSize: 9; Layout.preferredWidth: 82; elide: Text.ElideRight }
                Text { text: "Status"; color: "#9ab0c8"; font.pixelSize: 9; Layout.preferredWidth: 46; horizontalAlignment: Text.AlignHCenter }
                Text { text: "Meas"; color: "#9ab0c8"; font.pixelSize: 9; Layout.preferredWidth: 68; horizontalAlignment: Text.AlignHCenter }
                Text { text: "Limit"; color: "#9ab0c8"; font.pixelSize: 9; Layout.preferredWidth: 80; horizontalAlignment: Text.AlignHCenter }
                Text { text: "Result"; color: "#9ab0c8"; font.pixelSize: 9; Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; elide: Text.ElideRight }
            }
        }

        // CELL 任务迷你列表（TestCell 风格）
        ListView {
            id: cellTaskView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.taskModel

            delegate: Rectangle {
                width: cellTaskView.width
                height: 34
                color: status === "failed" ? "#3a1010"
                     : status === "succeeded" ? "#0a2a0a"
                     : status === "running" ? "#0a1040"
                     : "transparent"
                visible: (cellId === root.cellId)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    spacing: 4

                    Text {
                        text: measurementName && measurementName.length > 0 ? measurementName : workflowId
                        color: "#c0c0c0"
                        font.pixelSize: 9
                        Layout.preferredWidth: 82
                        elide: Text.ElideRight
                    }
                    Text {
                        text: status
                        color: status === "failed" ? "#ff5252"
                             : status === "succeeded" ? "#69f0ae"
                             : status === "running" ? "#00bcd4"
                             : "#8899aa"
                        font.pixelSize: 9
                        Layout.preferredWidth: 46
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        text: measuredValue && measuredValue.length > 0
                              ? (measuredValue + (measuredUnit && measuredUnit.length > 0 ? (" " + measuredUnit) : ""))
                              : "-"
                        color: "#e0e0e0"
                        font.pixelSize: 9
                        Layout.preferredWidth: 68
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                    Text {
                        text: (lowerLimit && upperLimit && lowerLimit.length > 0 && upperLimit.length > 0)
                              ? (lowerLimit + "~" + upperLimit)
                              : "-"
                        color: "#9ab0c8"
                        font.pixelSize: 9
                        Layout.preferredWidth: 80
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                    Text {
                        text: resultText && resultText.length > 0
                              ? resultText
                              : (analyzeMessage && analyzeMessage.length > 0 ? analyzeMessage : "-")
                        color: resultText === "FAIL" ? "#ff5252"
                             : resultText === "PASS" ? "#69f0ae"
                             : "#c0c0c0"
                        font.pixelSize: 9
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }
            }

            // 无数据提示
            Text {
                anchors.centerIn: parent
                text: "No tasks assigned"
                color: "#445566"
                font.pixelSize: 11
                visible: root.filteredCount === 0
            }
        }
    }

    // 更新任务计数和状态指示器
    Connections {
        target: root.taskModel
        function onRowsInserted() { updateCellStats() }
        function onDataChanged() { updateCellStats() }
        function onModelReset() { updateCellStats() }
    }

    Component.onCompleted: updateCellStats()

    function updateCellStats() {
        if (!root.taskModel) return;
        var count = 0;
        var hasFailed = false;
        var hasRunning = false;
        var allDone = true;
        for (var i = 0; i < root.taskModel.count; i++) {
            var t = root.taskModel.get(i);
            var cid = t.cellId;
            if (cid !== root.cellId) continue;
            count++;
            var st = t.status;
            if (st === "failed") hasFailed = true;
            if (st === "running") hasRunning = true;
            if (st !== "succeeded" && st !== "failed" && st !== "skipped") allDone = false;
        }
        root.filteredCount = count;
        taskCountText.text = count + " task" + (count !== 1 ? "s" : "");
        cellStatusDot.color = hasRunning ? "#00bcd4"
                            : hasFailed ? "#ff1744"
                            : count > 0 && allDone ? "#00c853"
                            : count > 0 ? "#ffab00"
                            : "#455a64";
    }
}
