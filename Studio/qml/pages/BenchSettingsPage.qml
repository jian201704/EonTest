import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    color: "#1a1a2e"

    property var benchSettings: null  // 由外部注入

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // ============================================================
        // 左侧：DUT 配置面板
        // ============================================================
        Rectangle {
            Layout.preferredWidth: parent.width * 0.48
            Layout.fillHeight: true
            color: "#16213e"
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                // 标题行
                RowLayout {
                    Text {
                        text: "🔌 DUT Settings"
                        color: "#00d2ff"
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "+ Add DUT"
                        flat: true
                        onClicked: benchSettings.dutModel.addEntry()
                        contentItem: Text {
                            text: parent.text
                            color: "#00c853"
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2a3a5e" }

                // DUT 表头
                RowLayout {
                    Repeater {
                        model: ["DUT ID", "Model", "Plugin", "Port", "On"]
                        delegate: Text {
                            text: modelData
                            color: "#8899aa"
                            font.pixelSize: 11
                            font.bold: true
                            Layout.preferredWidth: modelData === "Port" ? 100
                                                : (modelData === "On" ? 40 : 120)
                        }
                    }
                }

                // DUT 列表
                ListView {
                    id: dutList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: benchSettings ? benchSettings.dutModel : null
                    clip: true
                    spacing: 4

                    delegate: Rectangle {
                        width: dutList.width
                        height: 36
                        color: index % 2 == 0 ? "#1a2744" : "#16213e"
                        radius: 4

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 4

                            TextField {
                                text: dutId
                                Layout.preferredWidth: 120
                                font.pixelSize: 12; color: "#e0e0e0"
                                background: Rectangle { color: "#0f3460"; radius: 3 }
                                onEditingFinished: model.dutId = text
                            }
                            TextField {
                                text: modelName
                                Layout.preferredWidth: 120
                                font.pixelSize: 12; color: "#e0e0e0"
                                background: Rectangle { color: "#0f3460"; radius: 3 }
                                onEditingFinished: model.modelName = text
                            }
                            TextField {
                                text: pluginId
                                Layout.preferredWidth: 120
                                font.pixelSize: 12; color: "#e0e0e0"
                                background: Rectangle { color: "#0f3460"; radius: 3 }
                                onEditingFinished: model.pluginId = text
                            }
                            TextField {
                                text: connectionPort
                                Layout.preferredWidth: 100
                                font.pixelSize: 12; color: "#e0e0e0"
                                background: Rectangle { color: "#0f3460"; radius: 3 }
                                onEditingFinished: model.connectionPort = text
                            }
                            CheckBox {
                                checked: enabled
                                Layout.preferredWidth: 40
                                onCheckedChanged: model.enabled = checked
                            }
                            Button {
                                text: "✕"
                                flat: true
                                onClicked: benchSettings.dutModel.removeEntry(index)
                                contentItem: Text {
                                    text: parent.text
                                    color: "#ff5252"
                                    font.pixelSize: 12
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }
        }

        // ============================================================
        // 右侧：Instrument 配置面板
        // ============================================================
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#16213e"
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                // 标题行
                RowLayout {
                    Text {
                        text: "📡 Instrument Settings"
                        color: "#00d2ff"
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "+ Add Instrument"
                        flat: true
                        onClicked: benchSettings.instrumentModel.addEntry()
                        contentItem: Text {
                            text: parent.text
                            color: "#00c853"
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2a3a5e" }

                // 仪器表头
                RowLayout {
                    Repeater {
                        model: ["Name", "VISA Address", "Plugin", "Timeout(ms)", "On"]
                        delegate: Text {
                            text: modelData
                            color: "#8899aa"
                            font.pixelSize: 11
                            font.bold: true
                            Layout.preferredWidth: modelData === "VISA Address" ? 180
                                                : (modelData === "On" ? 40 : 110)
                        }
                    }
                }

                // 仪器列表
                ListView {
                    id: instList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: benchSettings ? benchSettings.instrumentModel : null
                    clip: true
                    spacing: 4

                    delegate: Rectangle {
                        width: instList.width
                        height: 36
                        color: index % 2 == 0 ? "#1a2744" : "#16213e"
                        radius: 4

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 4

                            TextField {
                                text: instrumentName
                                Layout.preferredWidth: 110
                                font.pixelSize: 12; color: "#e0e0e0"
                                background: Rectangle { color: "#0f3460"; radius: 3 }
                                onEditingFinished: model.instrumentName = text
                            }
                            TextField {
                                text: visaAddress
                                Layout.preferredWidth: 180
                                font.pixelSize: 12; color: "#e0e0e0"
                                background: Rectangle { color: "#0f3460"; radius: 3 }
                                onEditingFinished: model.visaAddress = text
                            }
                            TextField {
                                text: pluginId
                                Layout.preferredWidth: 110
                                font.pixelSize: 12; color: "#e0e0e0"
                                background: Rectangle { color: "#0f3460"; radius: 3 }
                                onEditingFinished: model.pluginId = text
                            }
                            SpinBox {
                                value: ioTimeout
                                from: 100; to: 30000; stepSize: 100
                                Layout.preferredWidth: 100
                                editable: true
                                onValueChanged: {
                                    if (model.ioTimeout !== value) model.ioTimeout = value
                                }
                            }
                            CheckBox {
                                checked: enabled
                                Layout.preferredWidth: 40
                                onCheckedChanged: model.enabled = checked
                            }
                            Button {
                                text: "✕"
                                flat: true
                                onClicked: benchSettings.instrumentModel.removeEntry(index)
                                contentItem: Text {
                                    text: parent.text
                                    color: "#ff5252"
                                    font.pixelSize: 12
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ============================================================
    // 底部操作栏
    // ============================================================
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56
        color: "#0f3460"

        RowLayout {
            anchors.centerIn: parent
            spacing: 16

            // Profile 选择
            Text { text: "Profile:"; color: "#8899aa"; font.pixelSize: 12 }
            ComboBox {
                id: profileCombo
                Layout.preferredWidth: 150
                model: benchSettings ? benchSettings.availableProfiles : []
                currentIndex: 0
                onActivated: benchSettings.currentProfile = currentText
                Component.onCompleted: if (model.length > 0) currentIndex = 0
            }

            // 操作按钮
            Button {
                text: "💾 Save"
                onClicked: benchSettings.save()
                background: Rectangle { color: "#00c853"; radius: 4 }
                contentItem: Text {
                    text: parent.text
                    color: "#ffffff"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Button {
                text: "📂 Load"
                onClicked: benchSettings.load()
                background: Rectangle { color: "#1976d2"; radius: 4 }
                contentItem: Text {
                    text: parent.text
                    color: "#ffffff"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Button {
                text: "+ New Profile"
                onClicked: {
                    var name = "profile_" + new Date().getTime()
                    benchSettings.createProfile(name)
                }
                background: Rectangle { color: "#e65100"; radius: 4 }
                contentItem: Text {
                    text: parent.text
                    color: "#ffffff"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Button {
                text: "🔄 Reset Defaults"
                onClicked: benchSettings.resetToDefaults()
                background: Rectangle { color: "#546e7a"; radius: 4 }
                contentItem: Text {
                    text: parent.text
                    color: "#ffffff"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
