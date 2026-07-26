import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// ============================================================
// TestCaseEditorPage — 测试用例图形编辑器
// JSON 仅在引擎执行时使用，不在 UI 中暴露给用户。
// ============================================================
Item {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true

    property string currentFilePath: ""

    // editorModel 由 MainWindow.qml 从 C++ 上下文属性注入，不在此处声明避免自遮蔽
    // property var editorModel: null  ← 删除此行，否则 QML 作用域优先解析为 null

    Rectangle {
        id: toolbar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 44; color: "#16213e"

        RowLayout {
            anchors.fill: parent; anchors.margins: 6; spacing: 8

            Text { text: "🎨 Test Case Editor"; color: "#00d2ff"; font.pixelSize: 15; font.bold: true }

            Rectangle { width: 1; height: 24; color: "#2a3a5e" }

            Button { text: "New"; onClicked: editorModel.newWorkflow("new-workflow"); flat: true }
            Button { text: "📂 Open"
                onClicked: {
                    var path = backend.browseForTestCase();
                    if (path !== "" && editorModel) {
                        var loaded = editorModel.loadFromFile(path);
                        if (loaded) backend.stageWorkflowForRun(editorModel.toJson(), editorModel.workflowId);
                    }
                }
                flat: true
                ToolTip.visible: hovered; ToolTip.text: "Open .xlsx test case" }
            Button {
                text: "💾 Save"
                onClicked: {
                    backend.saveCurrentWorkflow(editorModel.toJson(), editorModel.workflowId);
                }
                flat: true
            }
            Button {
                text: "▶ Run"
                enabled: editorModel.nodes.count > 0
                onClicked: {
                    var json = editorModel.toJson();
                    var stagedPath = backend.stageWorkflowForRun(json, editorModel.workflowId);
                    if (stagedPath !== "") backend.runSelected();
                }
                flat: true
            }

            Item { Layout.fillWidth: true }
            Text { text: editorModel.statusText; color: "#667788"; font.pixelSize: 11 }
        }
    }

    RowLayout {
        anchors.top: toolbar.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        spacing: 0

        // --- 左侧面板 ---
        Rectangle {
            Layout.preferredWidth: 180; Layout.fillHeight: true; color: "#16213e"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 6
                Text { text: "📦 Plugins"; color: "#8899aa"; font.pixelSize: 13; font.bold: true }

                Button {
                    text: "+ Add Step"; Layout.fillWidth: true
                    onClicked: {
                        if (!editorModel) return;
                        var count = editorModel.nodeCount;
                        var yPos = 40 + count * 120;
                        editorModel.addNode("step." + (count + 1), "sample.activity", 40, yPos);
                    }
                    background: Rectangle { color: "#00c853"; radius: 4 }
                }

                TextField { id: stepIdField; placeholderText: "step.step1"; Layout.fillWidth: true; font.pixelSize: 11; color: "#e0e0e0"; background: Rectangle { color: "#0f3460"; radius: 3 } }

                Repeater {
                    model: editorModel ? editorModel.availablePlugins : []
                    delegate: ItemDelegate {
                        required property string modelData
                        width: ListView.view ? ListView.view.width : 150
                        background: Rectangle { color: parent.hovered ? "#0f3460" : "transparent"; radius: 4 }
                        contentItem: Text { text: modelData; color: "#e0e0e0"; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        onClicked: {
                            if (!editorModel) return;
                            var count = editorModel.nodeCount;
                            var yPos = 40 + count * 120;
                            editorModel.addNode(stepIdField.text.length > 0 ? stepIdField.text : ("step." + (count + 1)), modelData, 40, yPos);
                            stepIdField.text = "";
                        }
                    }
                }

                Rectangle { height: 1; Layout.fillWidth: true; color: "#2a3a5e" }

                Text { text: "📋 Steps (" + (editorModel ? editorModel.nodeCount : 0) + ")"; color: "#8899aa"; font.pixelSize: 12; font.bold: true }

                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: editorModel ? editorModel.nodes : null
                    delegate: ItemDelegate {
                        width: ListView.view.width
                        highlighted: model.stepId === (editorModel ? editorModel.selStepId : "")
                        background: Rectangle { color: highlighted ? "#0f3460" : "transparent"; radius: 4 }
                        contentItem: RowLayout {
                            Rectangle { width: 8; height: 8; radius: 4; color: model.color }
                            Text { text: model.stepId; color: "#e0e0e0"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                        onClicked: { if (editorModel) editorModel.selectNode(model.stepId); }
                    }
                }
            }
        }

        // --- 画布 ---
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: "#0d1117"
            Flickable {
                id: canvasFlick; anchors.fill: parent; contentWidth: 2000; contentHeight: 1500; clip: true; boundsBehavior: Flickable.StopAtBounds
                Canvas {
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.strokeStyle = "#151d28"; ctx.lineWidth = 1;
                        for (var x = 0; x < width; x += 40) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke(); }
                        for (var y = 0; y < height; y += 40) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke(); }
                    }
                }
                WorkflowCanvasView {
                    id: canvasView; width: 2000; height: 1500
                    // editorModel/nodeModel/connModel 由组件内部从根上下文自动解析
                }
            }
            Row {
                anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 8
                Button { text: "+"; onClicked: canvasFlick.scale = Math.min(canvasFlick.scale * 1.2, 3.0) }
                Button { text: "\u2212"; onClicked: canvasFlick.scale = Math.max(canvasFlick.scale / 1.2, 0.3) }
                Button { text: "1:1"; onClicked: canvasFlick.scale = 1.0 }
            }
        }

        // --- 属性面板 ---
        StepPropertyPanel { Layout.preferredWidth: 280; Layout.fillHeight: true }
    }

}

