import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "pages"

ApplicationWindow {
    id: root
    title: "Eon Studio — Embedded Test Platform"
    width: 1360
    height: 820
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    property bool operatorMode: backend.userRole === "operator"

    // 暗色主题
    color: "#1a1a2e"

    // ============================================================
    // 自定义菜单栏（ApplicationWindow.menuBar 与 Material 兼容性差）
    // ============================================================
    header: ColumnLayout {
        spacing: 0

        // 菜单栏行
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#0a1520"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                spacing: 0

                // File 菜单
                MenuBarButton {
                    id: fileBtn
                    visible: backend.canConfigure
                    text: "📂 File"
                    onClicked: filePopup.opened ? filePopup.close() : filePopup.open()
                    Popup {
                        id: filePopup
                        y: parent.height
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        padding: 0
                        background: Rectangle { color: "#16213e"; border.color: "#2a3a5e"; border.width: 1; radius: 4 }
                        ColumnLayout {
                            spacing: 0
                            MenuRow { text: "Select DLL Directory..."; onClicked: { filePopup.close(); backend.browsePluginDirectory(backend.pluginDirectory) } }
                            MenuRow { text: "Select Script Directory..."; onClicked: { filePopup.close(); backend.browseDirectory(backend.workflowDirectory, "Select Script Directory") } }
                            MenuRow { text: "Select Report Directory..."; onClicked: { filePopup.close(); backend.browseDirectory(backend.reportDirectory, "Select Report Directory") } }
                            MenuRow { text: "Select State DB..."; onClicked: { filePopup.close(); backend.browseStateFile(backend.stateFilePath) } }
                            MenuSep {}
                            MenuRow { text: "Exit"; onClicked: { filePopup.close(); Qt.quit() } }
                        }
                    }
                }

                // Options 菜单
                MenuBarButton {
                    id: optBtn
                    visible: backend.canConfigure
                    text: "⚙ Options"
                    onClicked: optPopup.opened ? optPopup.close() : optPopup.open()
                    Popup {
                        id: optPopup
                        y: parent.height
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        padding: 0
                        background: Rectangle { color: "#16213e"; border.color: "#2a3a5e"; border.width: 1; radius: 4 }
                        ColumnLayout {
                            spacing: 0
                            MenuRow { text: "Browse Plugin Directory..."; onClicked: { optPopup.close(); backend.browsePluginDirectory(backend.pluginDirectory) } }
                            MenuRow { text: "Browse State DB..."; onClicked: { optPopup.close(); backend.browseStateFile(backend.stateFilePath) } }
                            MenuSep {}
                            MenuRow { text: "Stop on Failure"; checkable: true; checked: backend.stopOnFailure; onClicked: { backend.stopOnFailure = !backend.stopOnFailure } }
                        }
                    }
                }

                // CELLs 菜单
                MenuBarButton {
                    id: cellsBtn
                    visible: backend.canConfigure
                    text: "🖥 CELLs"
                    onClicked: cellsPopup.opened ? cellsPopup.close() : cellsPopup.open()
                    Popup {
                        id: cellsPopup
                        y: parent.height
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        padding: 0
                        background: Rectangle { color: "#16213e"; border.color: "#2a3a5e"; border.width: 1; radius: 4 }
                        ColumnLayout {
                            spacing: 0
                            MenuRow { text: "Add CELL  +"; onClicked: { cellsPopup.close(); if (backend.cellCount < 16) backend.cellCount++ } }
                            MenuRow { text: "Remove CELL  −"; enabled: backend.cellCount > 1; onClicked: { cellsPopup.close(); if (backend.cellCount > 1) backend.cellCount-- } }
                            MenuSep {}
                            MenuRow { text: "1 CELL"; onClicked: { cellsPopup.close(); backend.cellCount = 1 } }
                            MenuRow { text: "2 CELLs"; onClicked: { cellsPopup.close(); backend.cellCount = 2 } }
                            MenuRow { text: "4 CELLs"; onClicked: { cellsPopup.close(); backend.cellCount = 4 } }
                            MenuRow { text: "8 CELLs"; onClicked: { cellsPopup.close(); backend.cellCount = 8 } }
                        }
                    }
                }

                // Reporting 菜单
                MenuBarButton {
                    id: rptBtn
                    text: "📊 Reporting"
                    onClicked: rptPopup.opened ? rptPopup.close() : rptPopup.open()
                    Popup {
                        id: rptPopup
                        y: parent.height
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        padding: 0
                        background: Rectangle { color: "#16213e"; border.color: "#2a3a5e"; border.width: 1; radius: 4 }
                        ColumnLayout {
                            spacing: 0
                            MenuRow { text: "Dashboard"; onClicked: { rptPopup.close(); mainTabBar.currentIndex = 2 } }
                        }
                    }
                }

                // Help 菜单
                MenuBarButton {
                    id: helpBtn
                    text: "❓ Help"
                    onClicked: helpPopup.opened ? helpPopup.close() : helpPopup.open()
                    Popup {
                        id: helpPopup
                        y: parent.height
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        padding: 0
                        background: Rectangle { color: "#16213e"; border.color: "#2a3a5e"; border.width: 1; radius: 4 }
                        ColumnLayout {
                            spacing: 0
                            MenuRow { text: "About Eon Studio"; onClicked: { helpPopup.close(); backend.statusText = "Eon Studio v" + Qt.application.version } }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // CELL 数量指示
                Text {
                    text: "CELLs: " + backend.cellCount
                    color: "#8899aa"
                    font.pixelSize: 11
                    Layout.rightMargin: 8
                }
            }
        }

        // 工具栏行
        ToolBar {
            Layout.fillWidth: true
            background: Rectangle { color: "#16213e" }
            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 8

                Text {
                    text: "⚡ Eon Studio"
                    color: "#00d2ff"
                    font.pixelSize: 18
                    font.bold: true
                    Layout.leftMargin: 4
                }

                Rectangle { width: 1; height: 22; color: "#2a3a5e" }

                // 全局 Run / Abort
                Button {
                    text: backend.running ? "⏹ ABORT ALL" : "▶ RUN ALL"
                    onClicked: backend.running ? backend.stop() : backend.runSelected()
                    background: Rectangle {
                        color: backend.running ? "#ff1744" : "#00c853"
                        radius: 4
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }

                // 当前登录角色：点击后退出当前用户并重新登录
                Button {
                    text: "👤 " + backend.userName + " · " + backend.userRole + "  ▾"
                    flat: true
                    enabled: backend.authenticated
                    ToolTip.visible: hovered
                    ToolTip.text: "切换用户"
                    onClicked: backend.logout()
                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? "#ffffff" : "#9fb3c8"
                        font.pixelSize: 11
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Item { Layout.fillWidth: true }

                // 全局状态
                Rectangle {
                    radius: 4
                    color: backend.running ? "#00c853" : (backend.statusText.includes("Failed") ? "#ff1744" : "#455a64")
                    Layout.preferredWidth: 100; Layout.preferredHeight: 22
                    Text {
                        anchors.centerIn: parent
                        text: backend.statusText
                        color: "#ffffff"
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }
        }
    }

    // ============================================================
    // 主内容区域 (带 Tab 切换)
    // ============================================================
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Tab Bar
        TabBar {
            id: mainTabBar
            Layout.fillWidth: true
            background: Rectangle { color: "#0f3460" }

            TabButton {
                text: "\uD83D\uDE80 Run"
                width: 100
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? "#00d2ff" : "#8899aa"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            TabButton {
                text: "🎨 Test Case Editor"
                visible: !operatorMode
                width: 150
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? "#00d2ff" : "#8899aa"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            TabButton {
                text: "📊 Dashboard"
                width: 120
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? "#00d2ff" : "#8899aa"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            TabButton {
                text: "⚙ Bench"
                visible: !operatorMode
                width: 100
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? "#00d2ff" : "#8899aa"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // Stack Layout
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: mainTabBar.currentIndex

            // ============================================================
            // Page 0: Run — 多CELL独立面板 (JemTS 风格)
            // ============================================================
            ColumnLayout {
                spacing: 0

                // 遥测摘要卡片
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: "#1a1a2e"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        DashboardCard { title: "Test Cases"; value: backend.cntWorkflowsStarted; sub: "▶"; accentColor: "#2196f3" }
                        DashboardCard { title: "Passed";    value: backend.cntWorkflowsFinished; sub: "✓"; accentColor: "#00c853" }
                        DashboardCard { title: "Failed";    value: backend.cntWorkflowsFailed; sub: "✗"; accentColor: "#ff1744" }
                        DashboardCard { title: "Pass Rate"; value: Math.round(backend.passRate) + "%"; sub: ""; accentColor: "#ff9100" }
                        DashboardCard { title: "CELLs";     value: backend.cellCount; sub: ""; accentColor: "#00d2ff" }
                        DashboardCard { title: "Running";   value: backend.runningTaskCount; sub: ""; accentColor: "#4488ff" }
                    }
                }

                Rectangle { height: 1; Layout.fillWidth: true; color: "#2a3a5e" }

                // CELL 面板（保持原有自适应网格布局）
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6

                    Repeater {
                        model: Math.ceil(backend.cellCount / 2)
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 6

                            CellPanel {
                                cellId: index * 2
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: (index * 2) < backend.cellCount
                            }

                            CellPanel {
                                cellId: index * 2 + 1
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: (index * 2 + 1) < backend.cellCount
                            }
                        }
                    }
                }

                Rectangle { height: 1; Layout.fillWidth: true; color: "#2a3a5e" }

                // 全局日志面板（固定 160px）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 160
                    Layout.maximumHeight: 160
                    Layout.minimumHeight: 160
                    color: "#0d1117"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 4
                        RowLayout {
                            Text { text: "📜 Live Log"; color: "#8899aa"; font.pixelSize: 12; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Button { text: "Clear"; onClicked: backend.clearLog(); flat: true }
                        }
                        ScrollView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            TextArea {
                                id: logArea
                                text: backend.logHtml; readOnly: true
                                textFormat: TextEdit.RichText
                                background: Rectangle { color: "transparent" }
                                selectByMouse: true
                                selectByKeyboard: true
                                selectionColor: "#0f3460"
                                selectedTextColor: "#ffffff"
                            }
                        }
                    }
                    Connections {
                        target: backend
                        function onLogTextChanged() {
                            logArea.cursorPosition = logArea.length - 1;
                        }
                    }
                }
            }

            // ============================================================
            // Page 1: Test Case Editor
            // ============================================================
            WorkflowCanvasPage {
                id: workflowEditorPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                // editorModel 由 C++ 根上下文属性自动注入，无需显式传递
            }

            // ============================================================
            // Page 2: Parallel Dashboard（P1）
            // ============================================================
            DashboardPage {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            // ============================================================
            // Page 3: Bench Settings（P1-7 — 对标 OpenTAP Settings > Bench）
            // ============================================================
            BenchSettingsPage {
                Layout.fillWidth: true
                Layout.fillHeight: true
                benchSettings: benchSettingsCtx
            }
        } // end StackLayout
    } // end ColumnLayout
    footer: ToolBar {
        background: Rectangle { color: "#16213e" }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 6
            Text { text: "Events: " + backend.totalLogLines; color: "#667788"; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Text { text: "EonTest " + Qt.application.version; color: "#556677"; font.pixelSize: 11 }
        }
    }

    Dialog {
        id: loginDialog
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        title: "Eon Studio 登录"
        standardButtons: Dialog.NoButton
        visible: !backend.authenticated
        width: 360
        anchors.centerIn: Overlay.overlay

        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: "请输入账户和密码" }
            TextField { id: loginUser; Layout.fillWidth: true; placeholderText: "账户"; text: "engineer"; onAccepted: loginPassword.forceActiveFocus() }
            TextField { id: loginPassword; Layout.fillWidth: true; placeholderText: "密码"; echoMode: TextInput.Password; text: "engineer"; onAccepted: loginButton.clicked() }
            Label { id: loginError; text: ""; color: "#ff5252"; visible: text.length > 0 }
            Button {
                id: loginButton
                Layout.fillWidth: true
                text: "登录"
                onClicked: {
                    if (backend.login(loginUser.text, loginPassword.text)) {
                        loginError.text = ""; loginPassword.clear(); loginDialog.close()
                    } else loginError.text = "账户或密码错误"
                }
            }
            Label { text: "默认账户：admin / engineer / operator（密码同账户）"; color: "#718096"; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }
    }

    Connections {
        target: backend
        function onAuthenticationChanged() {
            if (backend.userRole === "operator") mainTabBar.currentIndex = 0
        }
    }
}
