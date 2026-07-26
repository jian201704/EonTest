import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    // editorModel 从 C++ 根上下文自动解析，无需父级传递
    color: "#16213e"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        Text {
            text: "Step Properties"
            color: "#8899aa"
            font.pixelSize: 14
            font.bold: true
        }
        Text {
            text: editorModel && editorModel.selStepId ? "Selected: " + editorModel.selStepId : "No step selected"
            color: "#667788"
            font.pixelSize: 11
        }
        Rectangle {
            height: 1
            Layout.fillWidth: true
            color: "#2a3a5e"
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                width: parent.width - 20
                spacing: 6
                visible: editorModel && editorModel.selStepId

                RowLayout {
                    Text { text: "Plugin"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    Text { text: editorModel ? editorModel.selPluginId : ""; color: "#e0e0e0"; font.pixelSize: 11 }
                }

                RowLayout {
                    Text { text: "Failure Policy"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    ComboBox {
                        model: ["fail_fast", "continue_on_error"]
                        currentIndex: editorModel && editorModel.selFailurePolicy === "continue_on_error" ? 1 : 0
                        onActivated: function(i) { if (editorModel) editorModel.selFailurePolicy = model[i] }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    Text { text: "Max Retries"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    SpinBox {
                        value: editorModel ? editorModel.selMaxRetries : 0
                        from: 0
                        to: 100
                        onValueChanged: { if (editorModel) editorModel.selMaxRetries = value }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    Text { text: "Timeout (ms)"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    SpinBox {
                        value: editorModel ? editorModel.selTimeoutMs : 0
                        from: 0
                        to: 600000
                        stepSize: 100
                        onValueChanged: { if (editorModel) editorModel.selTimeoutMs = value }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                    }
                }

                RowLayout {
                    Text { text: "Condition Key"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    TextField {
                        text: editorModel ? editorModel.selConditionKey : ""
                        onEditingFinished: { if (editorModel) editorModel.selConditionKey = text }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        color: "#e0e0e0"
                        background: Rectangle { color: "#0f3460"; radius: 3 }
                    }
                }

                RowLayout {
                    Text { text: "Condition Equals"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    TextField {
                        text: editorModel ? editorModel.selConditionEquals : ""
                        onEditingFinished: { if (editorModel) editorModel.selConditionEquals = text }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        color: "#e0e0e0"
                        background: Rectangle { color: "#0f3460"; radius: 3 }
                    }
                }

                RowLayout {
                    Text { text: "Parallel Group"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    TextField {
                        text: editorModel ? editorModel.selParallelGroup : ""
                        onEditingFinished: { if (editorModel) editorModel.selParallelGroup = text }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        color: "#e0e0e0"
                        background: Rectangle { color: "#0f3460"; radius: 3 }
                    }
                }

                RowLayout {
                    Text { text: "Compensation"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    TextField {
                        text: editorModel ? editorModel.selCompensationStep : ""
                        onEditingFinished: { if (editorModel) editorModel.selCompensationStep = text }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        color: "#e0e0e0"
                        background: Rectangle { color: "#0f3460"; radius: 3 }
                    }
                }

                Rectangle {
                    height: 1
                    Layout.fillWidth: true
                    color: "#2a3a5e"
                }
                Text { text: "Transitions"; color: "#8899aa"; font.pixelSize: 12; font.bold: true }

                RowLayout {
                    Text { text: "Success ->"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    TextField {
                        text: editorModel ? editorModel.selSuccessTarget : ""
                        onEditingFinished: { if (editorModel) editorModel.selSuccessTarget = text }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        color: "#e0e0e0"
                        background: Rectangle { color: "#0f3460"; radius: 3 }
                    }
                }

                RowLayout {
                    Text { text: "Failure ->"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    TextField {
                        text: editorModel ? editorModel.selFailureTarget : ""
                        onEditingFinished: { if (editorModel) editorModel.selFailureTarget = text }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        color: "#e0e0e0"
                        background: Rectangle { color: "#0f3460"; radius: 3 }
                    }
                }

                RowLayout {
                    Text { text: "Skipped ->"; color: "#667788"; font.pixelSize: 11; Layout.preferredWidth: 90 }
                    TextField {
                        text: editorModel ? editorModel.selSkippedTarget : ""
                        onEditingFinished: { if (editorModel) editorModel.selSkippedTarget = text }
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        color: "#e0e0e0"
                        background: Rectangle { color: "#0f3460"; radius: 3 }
                    }
                }
            }
        }
    }
}
