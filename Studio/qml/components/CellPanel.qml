import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ============================================================
// CellPanel — 独立 CELL 面板（JemTS 风格）
//
// 每个 CELL 具备：
//   - Serial Number (SN) 输入框
//   - 测试脚本/Excel 路径选择 (Browse)
//   - 配置按钮
//   - Start / Abort 按钮 + Pause / Resume
//   - 多 Tab 视图 (Browser / Status / Debug)
//   - 测试结果表格 (Test | Status | Meas | Limit | Result)
//   - 底部状态栏
// ============================================================
Rectangle {
    id: root

    // --- 属性 ---
    property int cellId: 0
    property string cellLabel: "CELL " + (cellId + 1)

    // 每 CELL 独立状态
    property string serialNumber: ""
    property string scriptPath: ""
    property bool isRunning: false
    property bool isPaused: false
    property string cellStatusText: "Ready..."

    // 颜色主题
    property color accentColor: {
        const colors = ["#00d2ff", "#ff9100", "#69f0ae", "#ff5252",
                        "#e040fb", "#448aff", "#ffab00", "#00e5ff"]
        return colors[cellId % colors.length]
    }
    property color headerColor: "#16213e"
    property color bgColor: "#0d1525"
    property color borderColor: isRunning ? accentColor : "#2a3a5e"

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumWidth: 280
    // 允许多 CELL 时按可用高度收缩；内部控件仍保留自己的最小高度。
    Layout.minimumHeight: 120
    radius: 8
    color: bgColor
    border.width: 2
    border.color: borderColor
    clip: true  // 防止内容整体溢出边框

    // ============================================================
    // 右键上下文菜单（替代配置按钮，含 TestCase 选择）
    // ============================================================
    property string savedSerial: ""  // Cancel 时恢复 SN

    Popup {
        id: ctxMenu
        x: 0; y: 0
        closePolicy: Popup.CloseOnEscape
        padding: 0
        background: Rectangle {
            color: "#16213e"
            border.color: "#2a3a5e"
            border.width: 1
            radius: 6
        }
        ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                color: "#0f3460"; radius: 6
                Text {
                    anchors.centerIn: parent
                    text: "⚙ " + root.cellLabel + " Config"
                    color: "#00d2ff"
                    font.pixelSize: 11; font.bold: true
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 240
                spacing: 4; Layout.margins: 8

                RowLayout { spacing: 6
                    Text { text: "Serial:"; color: "#8899aa"; font.pixelSize: 10; Layout.preferredWidth: 44 }
                    TextField {
                        id: ctxSerialField
                        Layout.fillWidth: true; font.pixelSize: 10
                        text: root.serialNumber; color: "#ffffff"
                        background: Rectangle { color: "#0f3460"; radius: 3 }
                    }
                }
                RowLayout { spacing: 6
                    Text { text: "Script:"; color: "#8899aa"; font.pixelSize: 10; Layout.preferredWidth: 44 }
                    Text {
                        text: root.scriptPath ? root.scriptPath.split('/').pop() : "(none)"
                        color: "#c0c0c0"; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight
                    }
                    Button {
                        text: "Browse..."
                        Layout.preferredWidth: 56; Layout.preferredHeight: 18
                        font.pixelSize: 9
                        background: Rectangle { color: "#0f3460"; radius: 3; border.width: 1; border.color: "#2a3a5e" }
                        contentItem: Text { text: "Browse..."; color: "#e0e0e0"; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter }
                        onClicked: {
                            var path = backend.browseForTestCase()
                            if (path !== "") {
                                root.scriptPath = path
                                if (backend) backend.setCellScript(cellId, path)
                            }
                        }
                    }
                }
                RowLayout { spacing: 6
                    Text { text: "State:"; color: "#8899aa"; font.pixelSize: 10; Layout.preferredWidth: 44 }
                    Text {
                        text: root.isPaused ? "Paused" : (root.isRunning ? "Running" : "Idle")
                        color: root.isRunning ? (root.isPaused ? "#ffab00" : "#00c853") : "#c0c0c0"
                        font.pixelSize: 10
                    }
                }
                RowLayout { spacing: 6
                    Text { text: "Steps:"; color: "#8899aa"; font.pixelSize: 10; Layout.preferredWidth: 44 }
                    Text { text: cellCompletedSteps() + ""; color: "#c0c0c0"; font.pixelSize: 10 }
                }
            }
            // OK / Cancel 按钮
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: "#0f1a2e"
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 6
                    Item { Layout.fillWidth: true }
                    Button {
                        id: okBtn
                        text: "OK"
                        Layout.preferredWidth: 50; Layout.preferredHeight: 22
                        font.pixelSize: 10
                        background: Rectangle { color: "#00c853"; radius: 4 }
                        contentItem: Text { text: "OK"; color: "#ffffff"; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                        onClicked: {
                            // 应用 SN 修改
                            root.serialNumber = ctxSerialField.text
                            if (backend) backend.setCellSerial(cellId, ctxSerialField.text)
                            ctxMenu.close()
                        }
                    }
                    Button {
                        id: cancelBtn
                        text: "Cancel"
                        Layout.preferredWidth: 56; Layout.preferredHeight: 22
                        font.pixelSize: 10
                        background: Rectangle { color: "#455a64"; radius: 4 }
                        contentItem: Text { text: "Cancel"; color: "#e0e0e0"; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter }
                        onClicked: {
                            // 恢复 SN 到打开前的值
                            ctxSerialField.text = root.savedSerial
                            ctxMenu.close()
                        }
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: backend ? backend.canConfigure : false
        acceptedButtons: Qt.RightButton
        propagateComposedEvents: false
        onPressed: function(mouse) {
            // 打开时保存当前 SN，供 Cancel 恢复
            root.savedSerial = root.serialNumber
            ctxSerialField.text = root.serialNumber
            ctxMenu.x = mouse.x; ctxMenu.y = mouse.y
            ctxMenu.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ============================================================
        // 顶部标题栏：CELL 标签 + SN 输入 + 状态指示
        // ============================================================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            Layout.minimumHeight: 34
            color: headerColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                // CELL 图标
                Rectangle {
                    width: 26; height: 26; radius: 13
                    color: isRunning ? (isPaused ? "#ffab00" : "#00c853")
                         : (cellStatusText.includes("Fail") ? "#ff5252" : "#455a64")
                    border.width: 2; border.color: accentColor
                    Text {
                        anchors.centerIn: parent
                        text: "⬡"
                        color: "#ffffff"
                        font.pixelSize: 13
                    }
                }

                Text {
                    text: cellLabel
                    color: accentColor
                    font.pixelSize: 13
                    font.bold: true
                    elide: Text.ElideRight
                }

                Rectangle { width: 1; height: 18; color: "#2a3a5e" }

                // SN 标签 + 输入框
                Text {
                    text: "SN:"
                    color: "#8899aa"
                    font.pixelSize: 11
                }

                TextField {
                    id: snField
                    Layout.fillWidth: true
                    Layout.maximumWidth: 120
                    font.pixelSize: 11
                    color: "#ffffff"
                    placeholderText: "Scan SN..."
                    placeholderTextColor: "#556677"
                    leftPadding: 6; topPadding: 3; bottomPadding: 3
                    background: Rectangle {
                        color: "#0f3460"
                        radius: 4
                        border.width: 1
                        border.color: snField.activeFocus ? accentColor : "#2a3a5e"
                    }
                    onTextChanged: {
                        root.serialNumber = text
                        if (backend) backend.setCellSerial(cellId, text)
                    }
                    onAccepted: {
                        if (!root.isRunning && root.scriptPath !== "") {
                            startCell()
                        }
                    }
                }

                // CELL 运行状态指示（紧凑徽章）
                Rectangle {
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 20
                    radius: 10
                    color: isRunning ? (isPaused ? "#ffab00" : "#00c853")
                         : (cellStatusText.includes("Fail") ? "#ff1744"
                         : "#455a64")
                    Text {
                        anchors.centerIn: parent
                        text: isPaused ? "PAUSED"
                             : isRunning ? "RUN"
                             : (cellStatusText.includes("Fail") ? "FAIL" : "IDLE")
                        color: "#ffffff"
                        font.pixelSize: 9
                        font.bold: true
                    }
                }
            }
        }

        // ============================================================
        // 脚本路径行 + 操作按钮
        // ============================================================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            Layout.minimumHeight: 24
            color: "#0a1220"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 4

                // 脚本路径显示（紧凑）
                TextField {
                    id: scriptPathField
                    Layout.fillWidth: true
                    Layout.minimumWidth: 30
                    font.pixelSize: 10
                    color: "#c0c0c0"
                    readOnly: true
                    leftPadding: 6; topPadding: 3; bottomPadding: 3
                    text: root.scriptPath !== ""
                          ? root.scriptPath.split('/').pop().replace('.xlsx','').replace('.workflow.json','')
                          : "(No test case)"
                    placeholderText: "Select test script..."
                    placeholderTextColor: "#445566"
                    background: Rectangle {
                        color: "#0f1a2e"
                        radius: 3
                        border.width: 1
                        border.color: root.scriptPath !== "" ? accentColor : "#2a3a5e"
                    }
                    ToolTip {
                        visible: scriptPathField.hovered && root.scriptPath !== ""
                        text: root.scriptPath
                        delay: 500
                    }
                }

                // Browse 按钮
                Button {
                    id: browseBtn
                    text: "..."
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 22
                    enabled: !root.isRunning && (backend ? backend.canConfigure : false)
                    background: Rectangle {
                        color: browseBtn.enabled
                               ? (browseBtn.hovered ? "#1b5e8a" : "#163b5c")
                               : "#1a2430"
                        radius: 4
                        border.width: 1; border.color: browseBtn.enabled ? "#4d86b3" : "#2a3a5e"
                    }
                    contentItem: Text {
                        text: browseBtn.text
                        color: browseBtn.enabled ? "#e0e0e0" : "#556677"
                        font.pixelSize: 12; font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        var path = backend.browseForTestCase()
                        if (path !== "") {
                            root.scriptPath = path
                            if (backend) backend.setCellScript(cellId, path)
                        }
                    }
                }

                // Pause / Resume 按钮（运行时显示）
                Button {
                    id: pauseBtn
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 22
                    visible: root.isRunning && (backend ? backend.canConfigure : false)
                    enabled: root.isRunning && (backend ? backend.canConfigure : false)
                    background: Rectangle {
                        color: root.isPaused ? "#008c9e" : "#b76500"
                        radius: 4
                        border.width: 1
                        border.color: root.isPaused ? "#55eaff" : "#ffc266"
                    }
                    contentItem: Text {
                        text: root.isPaused ? "▶" : "⏸"
                        color: "#ffffff"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (root.isPaused) resumeCell()
                        else pauseCell()
                    }
                }

                // Start / Abort 按钮
                Button {
                    id: startAbortBtn
                    Layout.preferredWidth: 54
                    Layout.preferredHeight: 24
                    background: Rectangle {
                        color: root.isRunning ? "#ff1744"
                             : (root.scriptPath === "" ? "#3a3a3a" : "#00c853")
                        radius: 5
                        border.width: 1
                        border.color: root.isRunning ? "#ff8a9b" : (root.scriptPath === "" ? "#667788" : "#7dffad")
                        Behavior on color { ColorAnimation { duration: 200 } }
                    }
                    contentItem: Text {
                        text: root.isRunning ? "✕ ABORT" : "▶ START"
                        color: "#ffffff"
                        font.pixelSize: 10
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (root.isRunning) abortCell()
                        else startCell()
                    }
                }
            }
        }

        // ============================================================
        // Tab Bar（紧凑）
        // ============================================================
        TabBar {
            id: cellTabBar
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            Layout.minimumHeight: 22
            background: Rectangle { color: "#0b1829"; border.color: "#36516d"; border.width: 1 }

            TabButton {
                text: "Browser"
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? accentColor : "#8899aa"
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.checked ? "#1b4164" : (parent.hovered ? "#142d47" : "transparent")
                    border.width: parent.checked ? 1 : 0
                    border.color: accentColor
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 2
                        color: parent.parent.checked ? accentColor : "transparent"
                    }
                }
            }
            TabButton {
                text: "Status"
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? accentColor : "#8899aa"
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.checked ? "#1b4164" : (parent.hovered ? "#142d47" : "transparent")
                    border.width: parent.checked ? 1 : 0
                    border.color: accentColor
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 2
                        color: parent.parent.checked ? accentColor : "transparent"
                    }
                }
            }
            TabButton {
                text: "Debug"
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? accentColor : "#8899aa"
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.checked ? "#1b4164" : (parent.hovered ? "#142d47" : "transparent")
                    border.width: parent.checked ? 1 : 0
                    border.color: accentColor
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 2
                        color: parent.parent.checked ? accentColor : "transparent"
                    }
                }
            }
        }

        // ============================================================
        // Tab 内容区
        // ============================================================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true  // Tab 内容不溢出

            StackLayout {
                anchors.fill: parent
                currentIndex: cellTabBar.currentIndex

                // Tab 0: Browser — 测试结果表格
                ColumnLayout {
                    spacing: 0
                    // 表头
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 20
                        Layout.minimumHeight: 20
                        color: "#1a2a44"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 2; anchors.rightMargin: 2
                            spacing: 1
                            Text { text: "TestName"; color: "#6688aa"; font.pixelSize: 9; font.bold: true
                                Layout.fillWidth: true; Layout.preferredWidth: 42; leftPadding: 4; elide: Text.ElideRight }
                            Text { text: "Status"; color: "#6688aa"; font.pixelSize: 9; font.bold: true
                                Layout.preferredWidth: 32; horizontalAlignment: Text.AlignHCenter }
                            Text { text: "MeasureName"; color: "#6688aa"; font.pixelSize: 9; font.bold: true
                                Layout.preferredWidth: 56; horizontalAlignment: Text.AlignHCenter }
                            Text { text: "Meas"; color: "#6688aa"; font.pixelSize: 9; font.bold: true
                                Layout.preferredWidth: 48; horizontalAlignment: Text.AlignHCenter }
                            Text { text: "Unit"; color: "#6688aa"; font.pixelSize: 9; font.bold: true
                                Layout.preferredWidth: 30; horizontalAlignment: Text.AlignHCenter }
                            Text { text: "Limit"; color: "#6688aa"; font.pixelSize: 9; font.bold: true
                                Layout.preferredWidth: 48; horizontalAlignment: Text.AlignHCenter }
                            Text { text: "Result"; color: "#6688aa"; font.pixelSize: 9; font.bold: true
                                Layout.preferredWidth: 36; horizontalAlignment: Text.AlignHCenter }
                            Text { text: "耗时"; color: "#6688aa"; font.pixelSize: 9; font.bold: true
                                Layout.preferredWidth: 36; horizontalAlignment: Text.AlignHCenter }
                        }
                    }

                    ListView {
                        id: cellResultView
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        model: backend ? backend.taskListModel : null

                        delegate: Rectangle {
                            width: cellResultView.width; height: 20
                            property bool resultRowHovered: false
                            color: {
                                if (model.cellId !== root.cellId) return "transparent"
                                switch (model.status) {
                                    case "failed": return "#3a1010"
                                    case "succeeded": return "#0a2a0a"
                                    default: return "transparent"
                                }
                            }
                            visible: model.cellId === root.cellId

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 2; anchors.rightMargin: 2
                                spacing: 1

                                // TestName — 显示 stepId
                                Text {
                                    text: (model.workflowPath && model.workflowPath.length > 0)
                                        ? model.workflowPath : "-"
                                    color: "#c0c0c0"; font.pixelSize: 9
                                    Layout.fillWidth: true; Layout.preferredWidth: 42; leftPadding: 4; elide: Text.ElideRight
                                }
                                // Status
                                Text {
                                    text: model.status || "-"
                                    color: model.status === "failed" ? "#ff5252"
                                        : model.status === "succeeded" ? "#69f0ae"
                                        : model.status === "running" ? "#00bcd4" : "#8899aa"
                                    font.pixelSize: 9; Layout.preferredWidth: 32; horizontalAlignment: Text.AlignHCenter
                                }
                                // MeasureName — pluginId
                                Text {
                                    text: (model.measurementName && model.measurementName.length > 0)
                                        ? model.measurementName : "-"
                                    color: "#b0b0b0"; font.pixelSize: 9
                                    Layout.preferredWidth: 56; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                                }
                                // Meas — measuredValue
                                Text {
                                    text: (model.measuredValue && model.measuredValue.length > 0)
                                        ? model.measuredValue : "-"
                                    color: "#e0e0e0"; font.pixelSize: 9
                                    Layout.preferredWidth: 48; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                                }
                                // Unit — measuredUnit
                                Text {
                                    text: (model.measuredUnit && model.measuredUnit.length > 0)
                                        ? model.measuredUnit : ""
                                    color: "#9ab0c8"; font.pixelSize: 9
                                    Layout.preferredWidth: 30; horizontalAlignment: Text.AlignHCenter
                                }
                                // Limit
                                Text {
                                    text: (model.lowerLimit && model.upperLimit && model.lowerLimit.length > 0 && model.upperLimit.length > 0)
                                        ? (model.lowerLimit + "~" + model.upperLimit) : "-"
                                    color: "#9ab0c8"; font.pixelSize: 9
                                    Layout.preferredWidth: 48; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                                }
                                // Result
                                Text {
                                    text: model.resultText && model.resultText.length > 0 ? model.resultText
                                        : (model.analyzeMessage && model.analyzeMessage.length > 0 ? model.analyzeMessage : "-")
                                    color: model.resultText === "FAIL" ? "#ff5252"
                                        : model.resultText === "PASS" ? "#69f0ae" : "#c0c0c0"
                                    font.pixelSize: 9; Layout.preferredWidth: 36
                                    horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                                }
                                // 耗时 — elapsedMs
                                Text {
                                    text: (model.elapsedMs && model.elapsedMs > 0)
                                        ? (model.elapsedMs.toFixed(0) + "ms") : "-"
                                    color: "#8899aa"; font.pixelSize: 9
                                    Layout.preferredWidth: 36; horizontalAlignment: Text.AlignHCenter
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                                onEntered: resultRowHovered = true
                                onExited: resultRowHovered = false
                            }
                            ToolTip {
                                visible: resultRowHovered && model.resultItems && model.resultItems.length > 1
                                delay: 350
                                text: {
                                    if (!model.resultItems) return ""
                                    var values = []
                                    for (var i = 0; i < model.resultItems.length; ++i) {
                                        var item = model.resultItems[i]
                                        values.push((item.name || "measurement") + " = " +
                                                     (item.value !== undefined ? item.value : "") +
                                                     (item.unit ? " " + item.unit : ""))
                                    }
                                    return values.join("\n")
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: root.scriptPath === "" ? "Select test case & press START"
                                  : (!root.isRunning ? "Ready — press START" : "Waiting...")
                            color: "#445566"; font.pixelSize: 10
                            visible: cellResultView.count === 0 || !hasVisibleItems()
                        }

                        function hasVisibleItems() {
                            return backend ? backend.cellTaskCount(root.cellId) > 0 : false
                        }
                    }
                }

                // Tab 1: Status — 状态概览
                Rectangle {
                    color: "#0a111a"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 6; spacing: 3
                        Text { text: "📊 Cell " + (root.cellId + 1); color: "#e0e0e0"; font.pixelSize: 11; font.bold: true }

                        GridLayout {
                            columns: 2; rowSpacing: 3; columnSpacing: 6
                            Repeater {
                                model: [
                                    { label: "SN:", value: root.serialNumber || "(not set)" },
                                    { label: "Script:", value: root.scriptPath ? root.scriptPath.split('/').pop() : "(none)" },
                                    { label: "State:", value: root.isPaused ? "Paused" : (root.isRunning ? "Running" : "Idle") },
                                    { label: "Steps:", value: cellCompletedSteps() + "" }
                                ]
                                RowLayout { spacing: 4
                                    Text { text: modelData.label; color: "#667788"; font.pixelSize: 10; Layout.preferredWidth: 50 }
                                    Text { text: modelData.value; color: "#c0c0c0"; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                                }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                // Tab 2: Debug — 遥测日志
                Rectangle {
                    color: "#0a111a"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 4; spacing: 2
                        Text { text: "🐛 Debug"; color: "#e0e0e0"; font.pixelSize: 10; font.bold: true }
                        ScrollView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            TextArea {
                                readOnly: true; font.pixelSize: 9
                                font.family: "Consolas, Courier New, monospace"
                                color: "#a0a0a0"
                                text: backend ? backend.cellDebugLog(root.cellId) : ""
                                background: Rectangle { color: "transparent" }
                                textFormat: TextEdit.PlainText; selectByMouse: true
                            }
                        }
                    }
                }
            }
        }

        // ============================================================
        // 底部状态栏（紧凑）
        // ============================================================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 18
            Layout.minimumHeight: 18
            color: "#0f2137"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6; anchors.rightMargin: 6
                spacing: 4

                Rectangle { width: 6; height: 6; radius: 3
                    color: root.isRunning ? (root.isPaused ? "#ffab00" : "#00ff88")
                        : (root.cellStatusText.includes("Fail") ? "#ff5252" : "#8899aa") }

                Text { text: root.cellStatusText
                    color: root.isRunning ? (root.isPaused ? "#ffab00" : "#00ff88")
                        : (root.cellStatusText.includes("Fail") ? "#ff5252" : "#8899aa")
                    font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true }

                Text { text: "Steps:" + cellCompletedSteps(); color: "#556677"; font.pixelSize: 9 }
                Text { text: "|"; color: "#334455"; font.pixelSize: 9 }
                Text { text: root.cellLabel; color: accentColor; font.pixelSize: 9; font.bold: true }
            }
        }
    }

    // ============================================================
    // 辅助函数
    // ============================================================

    function cellCompletedSteps() {
        return backend ? backend.cellTaskCount(root.cellId) : 0
    }

    function startCell() {
        if (root.isRunning) return
        if (root.scriptPath === "") {
            root.cellStatusText = "No test case selected!"
            return
        }
        if (backend) {
            backend.setCellScript(root.cellId, root.scriptPath)
            backend.setCellSerial(root.cellId, root.serialNumber)
            backend.runCell(root.cellId)
        }
        root.isRunning = true
        root.isPaused = false
        root.cellStatusText = "Running..."
    }

    function abortCell() {
        if (backend) backend.stopCell(root.cellId)
        root.isRunning = false
        root.isPaused = false
        root.cellStatusText = "Aborted"
    }

    function pauseCell() {
        if (backend) backend.pauseCell(root.cellId)
        root.isPaused = true
        root.cellStatusText = "Paused"
    }

    function resumeCell() {
        if (backend) backend.resumeCell(root.cellId)
        root.isPaused = false
        root.cellStatusText = "Running..."
    }

    Connections {
        target: backend
        function onCellStateChanged(cellId, running, statusText) {
            if (cellId === root.cellId) {
                root.isRunning = running
                root.cellStatusText = statusText
                if (!running) root.isPaused = false
            }
        }
        function onCellSerialChanged(cellId, sn) {
            if (cellId === root.cellId) { root.serialNumber = sn; snField.text = sn }
        }
    }

    Connections {
        target: backend ? backend.taskListModel : null
        function onRowsInserted() {}
        function onDataChanged() { updateCellStatusFromTasks() }
        function onModelReset() { if (!root.isRunning) root.cellStatusText = "Ready..." }
    }

    function updateCellStatusFromTasks() {
        if (!backend) return
        if (backend.cellTaskCount(root.cellId) === 0) return
        if (backend.cellHasFailed(root.cellId)) {
            root.cellStatusText = "Failed"
            root.isRunning = false; root.isPaused = false
        } else if (backend.cellAllDone(root.cellId)) {
            root.cellStatusText = "Completed ✓"
            root.isRunning = false; root.isPaused = false
        }
    }
}
