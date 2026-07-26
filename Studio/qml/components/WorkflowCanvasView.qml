import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ============================================================
// WorkflowCanvasView — 画布内节点渲染 + 连线
// ============================================================
Item {
    id: root

    // 直接从 C++ 根上下文解析，无需父级传递（避免属性遮蔽）
    // editorModel 由 main.cpp setContextProperty 注入
    property var nodeModel: editorModel ? editorModel.nodes : null
    property var connModel: editorModel ? editorModel.connections : null

    // 连线画布
    Canvas {
        id: connectionCanvas
        anchors.fill: parent
        z: 0

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);

            if (!connModel) return;

            // 构建 nodeId -> centerPos 查找表
            var nodePos = {};
            for (var i = 0; i < nodeModel.count; i++) {
                var nd = nodeModel.get(i);
                nodePos[nd.stepId] = { x: nd.x + 70, y: nd.y + 25 }; // node center
            }

            for (var j = 0; j < connModel.count; j++) {
                var cd = connModel.get(j);
                var fromId = cd.fromStepId;
                var toId   = cd.toStepId;
                var cType  = cd.connType;

                var from = nodePos[fromId];
                var to   = nodePos[toId];
                if (!from || !to) continue;

                // 连线颜色
                ctx.strokeStyle = cType === "success" ? "#00c853"
                                : cType === "failure" ? "#ff1744"
                                : cType === "skipped" ? "#ffab00"
                                : "#9c27b0"; // compensation
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.moveTo(from.x + 70, from.y); // right edge
                // 贝塞尔曲线
                var cx1 = from.x + 70 + Math.abs(to.x - from.x) * 0.5;
                var cx2 = to.x - Math.abs(to.x - from.x) * 0.5;
                ctx.bezierCurveTo(cx1, from.y, cx2, to.y, to.x, to.y);
                ctx.stroke();

                // 箭头
                ctx.fillStyle = ctx.strokeStyle;
                var arrowSize = 6;
                ctx.beginPath();
                ctx.moveTo(to.x, to.y);
                ctx.lineTo(to.x - arrowSize, to.y - arrowSize/2);
                ctx.lineTo(to.x - arrowSize, to.y + arrowSize/2);
                ctx.closePath();
                ctx.fill();
            }
        }
    }

    // 节点渲染
    Repeater {
        model: nodeModel

        Rectangle {
            id: nodeRect
            x: model.x
            y: model.y
            width: 140
            height: 50
            radius: 8
            border.width: model.stepId === (editorModel ? editorModel.selStepId : "") ? 3 : 1
            border.color: model.stepId === (editorModel ? editorModel.selStepId : "") ? "#ffffff" : Qt.darker(model.color, 1.2)
            color: model.color

            // 入口标记
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                width: 24; height: 18
                radius: 4
                color: model.stepId === (editorModel ? editorModel.entryStepId : "") ? "#ffd700" : "transparent"
                visible: model.stepId === (editorModel ? editorModel.entryStepId : "")
                Text {
                    anchors.centerIn: parent
                    text: "▶"
                    color: "#000"
                    font.pixelSize: 10
                }
            }

            // 标签
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 0
                Text {
                    text: model.label || model.stepId
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: model.stepId
                    color: "#ccddff"
                    font.pixelSize: 9
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            // 拖拽
            DragHandler {
                id: dragHandler
                onActiveChanged: {
                    if (active && editorModel) {
                        editorModel.selectNode(model.stepId);
                    }
                }
                onCentroidChanged: {
                    if (editorModel) {
                        editorModel.moveNode(model.stepId, nodeRect.x, nodeRect.y);
                        connectionCanvas.requestPaint();
                    }
                }
            }

            // 单击选中
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onClicked: {
                    if (editorModel) editorModel.selectNode(model.stepId);
                }
            }

            // 拖拽中实时刷新连线
            onXChanged: {
                if (dragHandler.active) connectionCanvas.requestPaint();
            }
            onYChanged: {
                if (dragHandler.active) connectionCanvas.requestPaint();
            }

            // 输出端口 (右侧圆点)
            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: -6
                width: 12; height: 12; radius: 6
                color: "#00e5ff"
                border.width: 1
                border.color: "#004466"

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.CrossCursor
                    // 连线模式（从输出端口拖拽到目标节点）
                }
            }
        }
    }

    // 连线更新
    Connections {
        target: editorModel
        function onConnectionChanged() { connectionCanvas.requestPaint(); }
    }

    // 单击空白区域取消选中
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: {
            if (editorModel) editorModel.deselectNode();
        }
    }
}
