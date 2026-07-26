#include "eon/studio/WorkflowEditorModel.h"
#include "eon/domain/WorkflowDefinition.h"
#include "eon/infra/XlsxParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>

namespace eon::studio {

const QStringList WorkflowEditorModel::kNodeColors = {
    "#2196f3", "#00c853", "#ff9100", "#9c27b0", "#00bcd4",
    "#ff1744", "#651fff", "#ffab00", "#00e5ff", "#76ff03"
};

// ============================================================================
// EditorNodeListModel
// ============================================================================

EditorNodeListModel::EditorNodeListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int EditorNodeListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : nodes_.size();
}

QVariant EditorNodeListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= nodes_.size()) return {};
    const auto& n = nodes_.at(index.row());

    switch (role) {
    case StepIdRole:          return n.stepId;
    case PluginIdRole:        return n.pluginId;
    case LabelRole:           return n.label;
    case XRole:               return n.x;
    case YRole:               return n.y;
    case ColorRole:           return n.color;
    case MaxRetriesRole:      return n.maxRetries;
    case TimeoutMsRole:       return n.timeoutMs;
    case FailurePolicyRole:   return n.failurePolicy;
    case ParallelGroupIdRole: return n.parallelGroupId;
    case ConditionKeyRole:    return n.conditionKey;
    case ConditionEqualsRole: return n.conditionEquals;
    case CompensationStepIdRole: return n.compensationStepId;
    default: break;
    }
    return {};
}

QVariant EditorNodeListModel::get(int index) const {
    if (index < 0 || index >= nodes_.size()) return {};
    const auto& n = nodes_.at(index);
    QVariantMap map;
    map["stepId"] = n.stepId;
    map["pluginId"] = n.pluginId;
    map["label"] = n.label;
    map["x"] = n.x;
    map["y"] = n.y;
    map["color"] = n.color;
    map["maxRetries"] = n.maxRetries;
    map["timeoutMs"] = n.timeoutMs;
    map["failurePolicy"] = n.failurePolicy;
    map["parallelGroupId"] = n.parallelGroupId;
    map["conditionKey"] = n.conditionKey;
    map["conditionEquals"] = n.conditionEquals;
    map["compensationStepId"] = n.compensationStepId;
    return map;
}

bool EditorNodeListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= nodes_.size()) return false;
    auto& n = nodes_[index.row()];

    switch (role) {
    case XRole: n.x = value.toDouble(); break;
    case YRole: n.y = value.toDouble(); break;
    case PluginIdRole: n.pluginId = value.toString(); break;
    case MaxRetriesRole: n.maxRetries = value.toInt(); break;
    case TimeoutMsRole: n.timeoutMs = value.toInt(); break;
    case FailurePolicyRole: n.failurePolicy = value.toString(); break;
    case ConditionKeyRole: n.conditionKey = value.toString(); break;
    case ConditionEqualsRole: n.conditionEquals = value.toString(); break;
    default: return false;
    }
    emit dataChanged(index, index, {role});
    return true;
}

QHash<int, QByteArray> EditorNodeListModel::roleNames() const {
    return {
        {StepIdRole, "stepId"}, {PluginIdRole, "pluginId"}, {LabelRole, "label"},
        {XRole, "x"}, {YRole, "y"}, {ColorRole, "color"},
        {MaxRetriesRole, "maxRetries"}, {TimeoutMsRole, "timeoutMs"},
        {FailurePolicyRole, "failurePolicy"}, {ParallelGroupIdRole, "parallelGroupId"},
        {ConditionKeyRole, "conditionKey"}, {ConditionEqualsRole, "conditionEquals"},
        {CompensationStepIdRole, "compensationStepId"}
    };
}

void EditorNodeListModel::addNode(const EditorNode& node) {
    beginInsertRows({}, nodes_.size(), nodes_.size());
    nodes_.append(node);
    endInsertRows();
    emit countChanged();
}

void EditorNodeListModel::removeNode(const QString& stepId) {
    for (int i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].stepId == stepId) {
            beginRemoveRows({}, i, i);
            nodes_.removeAt(i);
            endRemoveRows();
            emit countChanged();
            return;
        }
    }
}

void EditorNodeListModel::updateNodePosition(const QString& stepId, qreal x, qreal y) {
    for (int i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].stepId == stepId) {
            nodes_[i].x = x;
            nodes_[i].y = y;
            emit dataChanged(index(i), index(i), {XRole, YRole});
            return;
        }
    }
}

void EditorNodeListModel::clear() {
    beginResetModel();
    nodes_.clear();
    endResetModel();
    emit countChanged();
}

EditorNode* EditorNodeListModel::findNode(const QString& stepId) {
    for (auto& n : nodes_) {
        if (n.stepId == stepId) return &n;
    }
    return nullptr;
}

// ============================================================================
// EditorConnectionListModel
// ============================================================================

EditorConnectionListModel::EditorConnectionListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int EditorConnectionListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : connections_.size();
}

QVariant EditorConnectionListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= connections_.size()) return {};
    const auto& c = connections_.at(index.row());

    switch (role) {
    case FromStepIdRole: return c.fromStepId;
    case ToStepIdRole:   return c.toStepId;
    case TypeRole:       return c.type;
    default: break;
    }
    return {};
}

QHash<int, QByteArray> EditorConnectionListModel::roleNames() const {
    return {{FromStepIdRole, "fromStepId"}, {ToStepIdRole, "toStepId"}, {TypeRole, "connType"}};
}

QVariant EditorConnectionListModel::get(int index) const {
    if (index < 0 || index >= connections_.size()) return {};
    const auto& c = connections_.at(index);
    QVariantMap map;
    map["fromStepId"] = c.fromStepId;
    map["toStepId"] = c.toStepId;
    map["connType"] = c.type;
    return map;
}

void EditorConnectionListModel::addConnection(const EditorConnection& conn) {
    // 避免重复
    for (const auto& c : connections_) {
        if (c.fromStepId == conn.fromStepId && c.toStepId == conn.toStepId && c.type == conn.type)
            return;
    }
    beginInsertRows({}, connections_.size(), connections_.size());
    connections_.append(conn);
    endInsertRows();
    emit countChanged();
}

void EditorConnectionListModel::removeConnectionsFrom(const QString& stepId) {
    for (int i = connections_.size() - 1; i >= 0; --i) {
        if (connections_[i].fromStepId == stepId) {
            beginRemoveRows({}, i, i);
            connections_.removeAt(i);
            endRemoveRows();
        }
    }
    emit countChanged();
}

void EditorConnectionListModel::removeConnectionsTo(const QString& stepId) {
    for (int i = connections_.size() - 1; i >= 0; --i) {
        if (connections_[i].toStepId == stepId) {
            beginRemoveRows({}, i, i);
            connections_.removeAt(i);
            endRemoveRows();
        }
    }
    emit countChanged();
}

void EditorConnectionListModel::clear() {
    beginResetModel();
    connections_.clear();
    endResetModel();
    emit countChanged();
}

void EditorConnectionListModel::removeConnectionsByType(const QString& fromStepId, const QString& type) {
    for (int i = connections_.size() - 1; i >= 0; --i) {
        if (connections_[i].fromStepId == fromStepId && connections_[i].type == type) {
            beginRemoveRows({}, i, i);
            connections_.removeAt(i);
            endRemoveRows();
            emit countChanged();
            return;  // 每种类型只有一条连线
        }
    }
}

// ============================================================================
// WorkflowEditorModel
// ============================================================================

WorkflowEditorModel::WorkflowEditorModel(QObject* parent)
    : QObject(parent)
{
    connect(&nodeModel_, &QAbstractListModel::dataChanged, this, &WorkflowEditorModel::selectionChanged);
}

QString WorkflowEditorModel::workflowId() const      { return workflowId_; }
void WorkflowEditorModel::setWorkflowId(const QString& id) {
    if (workflowId_ != id) { workflowId_ = id; emit workflowIdChanged(); }
}
int WorkflowEditorModel::priority() const             { return priority_; }
void WorkflowEditorModel::setPriority(int p) {
    if (priority_ != p) { priority_ = p; emit priorityChanged(); }
}
QString WorkflowEditorModel::entryStepId() const       { return entryStepId_; }
void WorkflowEditorModel::setEntryStepId(const QString& id) {
    if (entryStepId_ != id) { entryStepId_ = id; emit entryStepIdChanged(); }
}
QStringList WorkflowEditorModel::availablePlugins() const { return availablePlugins_; }
void WorkflowEditorModel::setAvailablePlugins(const QStringList& p) {
    availablePlugins_ = p; emit availablePluginsChanged();
}
QString WorkflowEditorModel::statusText() const {
    return QString("%1 nodes, %2 connections")
        .arg(nodeModel_.rowCount()).arg(connModel_.rowCount());
}

// --- 节点操作 ---

void WorkflowEditorModel::addNode(const QString& stepId, const QString& pluginId, qreal x, qreal y) {
    EditorNode node;
    node.stepId = stepId.isEmpty()
        ? QString("step.%1").arg(++nodeCounter_)
        : stepId;
    node.pluginId = pluginId;
    node.label = pluginId;
    node.x = x;
    node.y = y;
    node.color = nextNodeColor(nodeCounter_);

    nodeModel_.addNode(node);
    emit nodeAdded(node.stepId);

    // 自动设为入口步骤（如果是第一个）
    if (nodeModel_.rowCount() == 1) {
        setEntryStepId(node.stepId);
    }

    emit statusTextChanged();
}

void WorkflowEditorModel::removeNode(const QString& stepId) {
    nodeModel_.removeNode(stepId);
    connModel_.removeConnectionsFrom(stepId);
    connModel_.removeConnectionsTo(stepId);

    if (entryStepId_ == stepId && nodeModel_.rowCount() > 0) {
        setEntryStepId(nodeModel_.nodes().first().stepId);
    }
    if (selectedStepId_ == stepId) {
        selectedStepId_.clear();
        emit selectionChanged();
    }

    emit nodeRemoved(stepId);
    emit connectionChanged();
    emit statusTextChanged();
}

void WorkflowEditorModel::moveNode(const QString& stepId, qreal x, qreal y) {
    nodeModel_.updateNodePosition(stepId, x, y);
    emit connectionChanged(); // 连线需重绘
}

void WorkflowEditorModel::selectNode(const QString& stepId) {
    selectedStepId_ = stepId;
    emit selectionChanged();
}

void WorkflowEditorModel::deselectNode() {
    selectedStepId_.clear();
    emit selectionChanged();
}

void WorkflowEditorModel::setTransition(const QString& from, const QString& to, const QString& type) {
    // 仅删除匹配类型的那一条连线（而非所有连线）
    connModel_.removeConnectionsByType(from, type);

    if (!to.isEmpty()) {
        EditorConnection conn;
        conn.fromStepId = from;
        conn.toStepId = to;
        conn.type = type;
        connModel_.addConnection(conn);
    }

    emit connectionChanged();
}

void WorkflowEditorModel::removeTransition(const QString& fromStepId, const QString& type) {
    for (int i = connModel_.connections().size() - 1; i >= 0; --i) {
        const auto& c = connModel_.connections()[i];
        if (c.fromStepId == fromStepId && c.type == type) {
            // 使用 begin/end remove
            connModel_.removeConnectionsFrom(fromStepId);
            break;
        }
    }
    emit connectionChanged();
}

// --- JSON 序列化 ---

bool WorkflowEditorModel::loadFromJson(const QString& jsonText) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit statusTextChanged();
        return false;
    }

    const QJsonObject root = doc.object();

    // 清空
    nodeModel_.clear();
    connModel_.clear();
    nodeCounter_ = 0;
    selectedStepId_.clear();

    setWorkflowId(root.value("workflowId").toString("loaded-workflow"));
    setPriority(root.value("priority").toInt(0));
    QString entryStep = root.value("entryStepId").toString();

    const QJsonArray steps = root.value("steps").toArray();
    int row = 0;
    for (const auto& s : steps) {
        const QJsonObject so = s.toObject();
        const QString sid = so.value("stepId").toString();
        const QString pid = so.value("pluginId").toString();

        EditorNode node;
        node.stepId = sid;
        node.pluginId = pid;
        node.label = pid.mid(pid.lastIndexOf('.') + 1);
        node.x = 80.0 + (row % 4) * 220.0;
        node.y = 60.0 + (row / 4) * 160.0;
        node.color = nextNodeColor(row);
        node.maxRetries = so.value("maxRetries").toInt(0);
        node.timeoutMs = so.value("timeoutMs").toInt(0);
        node.failurePolicy = so.value("failurePolicy").toString("fail_fast");
        node.parallelGroupId = so.value("parallelGroupId").toString();
        node.conditionKey = so.value("conditionKey").toString();
        node.conditionEquals = so.value("conditionEquals").toString();
        node.compensationStepId = so.value("compensationStepId").toString();

        nodeModel_.addNode(node);

        // 连线
        const QString onSuccess = so.value("onSuccessStepId").toString();
        const QString onFailure = so.value("onFailureStepId").toString();
        const QString onSkipped = so.value("onSkippedStepId").toString();

        if (!onSuccess.isEmpty()) {
            connModel_.addConnection({sid, onSuccess, "success"});
        }
        if (!onFailure.isEmpty()) {
            connModel_.addConnection({sid, onFailure, "failure"});
        }
        if (!onSkipped.isEmpty()) {
            connModel_.addConnection({sid, onSkipped, "skipped"});
        }
        if (!node.compensationStepId.isEmpty()) {
            connModel_.addConnection({sid, node.compensationStepId, "compensation"});
        }

        row++;
        nodeCounter_++;
    }

    setEntryStepId(entryStep.isEmpty() ? autoDetectEntryStep() : entryStep);
    const QString selectedAfterLoad = entryStep.isEmpty() ? autoDetectEntryStep() : entryStep;
    if (!selectedAfterLoad.isEmpty()) {
        selectNode(selectedAfterLoad);
    } else {
        emit selectionChanged();
    }
    emit statusTextChanged();
    emit connectionChanged();
    return true;
}

bool WorkflowEditorModel::loadFromFile(const QString& filePath) {
    QString normalizedPath = filePath.trimmed();
    const int firstLineBreak = normalizedPath.indexOf('\n');
    if (firstLineBreak >= 0) {
        normalizedPath = normalizedPath.left(firstLineBreak).trimmed();
    }
    if (normalizedPath.startsWith("file:///")) {
        normalizedPath = QUrl(normalizedPath).toLocalFile();
    }
    if (normalizedPath.startsWith('"') && normalizedPath.endsWith('"') && normalizedPath.size() > 1) {
        normalizedPath = normalizedPath.mid(1, normalizedPath.size() - 2);
    }

    QString suffix = QFileInfo(normalizedPath).suffix().toLower();

    if (suffix == "xlsx") {
        // ================================================================
        // 原生 .xlsx 格式 — 直接读取 Excel 三 Sheet
        // ================================================================
        eon::domain::WorkflowDefinition wf;
        QString error;
        if (!eon::infra::parseWorkflowDefinitionXlsx(normalizedPath, &wf, &error)) {
            return false;
        }

        // 清空当前编辑器内容
        nodeModel_.clear();
        connModel_.clear();
        nodeCounter_ = 0;
        selectedStepId_.clear();

        setWorkflowId(wf.workflowId);
        setEntryStepId(wf.entryStepId);

        for (int i = 0; i < wf.steps.size(); ++i) {
            const auto& s = wf.steps[i];

            EditorNode node;
            node.stepId = s.stepId;
            node.pluginId = s.pluginId;
            node.label = s.initialData.value("testItem").toString();
            if (node.label.isEmpty())
                node.label = s.pluginId.mid(s.pluginId.lastIndexOf('.') + 1);
            node.x = 80.0 + (i % 4) * 220.0;
            node.y = 60.0 + (i / 4) * 160.0;
            node.color = nextNodeColor(i);
            node.maxRetries = s.policy.maxRetries;
            node.timeoutMs = s.policy.timeoutMs;
            node.failurePolicy = (s.policy.failurePolicy == eon::domain::FailurePolicy::ContinueOnError)
                                 ? "continue_on_error" : "fail_fast";
            node.conditionKey = s.conditionKey;
            node.conditionEquals = s.conditionEquals;
            node.compensationStepId = s.compensationStepId;

            nodeModel_.addNode(node);

            // 连线
            if (!s.onSuccessStepId.isEmpty())
                connModel_.addConnection({s.stepId, s.onSuccessStepId, "success"});
            if (!s.onFailureStepId.isEmpty())
                connModel_.addConnection({s.stepId, s.onFailureStepId, "failure"});
            if (!s.onSkippedStepId.isEmpty())
                connModel_.addConnection({s.stepId, s.onSkippedStepId, "skipped"});
            if (!s.compensationStepId.isEmpty())
                connModel_.addConnection({s.stepId, s.compensationStepId, "compensation"});

            nodeCounter_++;
        }

        if (!wf.entryStepId.isEmpty())
            selectNode(wf.entryStepId);
        else
            emit selectionChanged();

        emit statusTextChanged();
        emit connectionChanged();
        return true;
    }

    // ================================================================
    // .json 兼容（旧格式）
    // ================================================================
    QFile file(normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QString jsonText = QString::fromUtf8(file.readAll());
    return loadFromJson(jsonText);
}

QString WorkflowEditorModel::toJson() const {
    QJsonObject root;
    root["workflowId"] = workflowId_;
    root["priority"] = priority_;
    root["entryStepId"] = entryStepId_;

    QJsonArray steps;
    for (const auto& n : nodeModel_.nodes()) {
        QJsonObject step;
        step["stepId"] = n.stepId;
        step["pluginId"] = n.pluginId;
        if (!n.parallelGroupId.isEmpty()) step["parallelGroupId"] = n.parallelGroupId;
        if (!n.conditionKey.isEmpty())     step["conditionKey"] = n.conditionKey;
        if (!n.conditionEquals.isEmpty())   step["conditionEquals"] = n.conditionEquals;
        if (!n.compensationStepId.isEmpty()) step["compensationStepId"] = n.compensationStepId;
        step["maxRetries"] = n.maxRetries;
        step["timeoutMs"] = n.timeoutMs;
        step["failurePolicy"] = n.failurePolicy;

        // 从连线反查转换目标
        for (const auto& c : connModel_.connections()) {
            if (c.fromStepId != n.stepId) continue;
            if (c.type == "success")       step["onSuccessStepId"] = c.toStepId;
            else if (c.type == "failure")  step["onFailureStepId"] = c.toStepId;
            else if (c.type == "skipped")  step["onSkippedStepId"] = c.toStepId;
        }

        steps.append(step);
    }
    root["steps"] = steps;

    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void WorkflowEditorModel::newWorkflow(const QString& wfId) {
    nodeModel_.clear();
    connModel_.clear();
    nodeCounter_ = 0;
    setWorkflowId(wfId);
    setPriority(0);
    setEntryStepId({});
    selectedStepId_.clear();
    emit selectionChanged();
    emit connectionChanged();
    emit statusTextChanged();
}

QString WorkflowEditorModel::autoDetectEntryStep() const {
    // 入口步骤 = 无入边的第一个节点
    QSet<QString> hasIncoming;
    for (const auto& c : connModel_.connections()) {
        hasIncoming.insert(c.toStepId);
    }
    for (const auto& n : nodeModel_.nodes()) {
        if (!hasIncoming.contains(n.stepId)) return n.stepId;
    }
    return nodeModel_.nodes().isEmpty() ? QString() : nodeModel_.nodes().first().stepId;
}

// --- 选中节点属性实现 ---

QString WorkflowEditorModel::selStepId() const {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_)
            return nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString();
    }
    return {};
}

QString WorkflowEditorModel::selPluginId() const {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_)
            return nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::PluginIdRole).toString();
    }
    return {};
}
void WorkflowEditorModel::setSelPluginId(const QString& v) {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_) {
            nodeModel_.setData(nodeModel_.index(i), v, EditorNodeListModel::PluginIdRole);
            emit selectionChanged();
            return;
        }
    }
}

int WorkflowEditorModel::selMaxRetries() const {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_)
            return nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::MaxRetriesRole).toInt();
    }
    return 0;
}
void WorkflowEditorModel::setSelMaxRetries(int v) {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_) {
            nodeModel_.setData(nodeModel_.index(i), v, EditorNodeListModel::MaxRetriesRole);
            emit selectionChanged();
            return;
        }
    }
}

int WorkflowEditorModel::selTimeoutMs() const {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_)
            return nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::TimeoutMsRole).toInt();
    }
    return 0;
}
void WorkflowEditorModel::setSelTimeoutMs(int v) {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_) {
            nodeModel_.setData(nodeModel_.index(i), v, EditorNodeListModel::TimeoutMsRole);
            emit selectionChanged();
            return;
        }
    }
}

QString WorkflowEditorModel::selFailurePolicy() const {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_)
            return nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::FailurePolicyRole).toString();
    }
    return "fail_fast";
}
void WorkflowEditorModel::setSelFailurePolicy(const QString& v) {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_) {
            nodeModel_.setData(nodeModel_.index(i), v, EditorNodeListModel::FailurePolicyRole);
            emit selectionChanged();
            return;
        }
    }
}

QString WorkflowEditorModel::selConditionKey() const {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_)
            return nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::ConditionKeyRole).toString();
    }
    return {};
}
void WorkflowEditorModel::setSelConditionKey(const QString& v) {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_) {
            nodeModel_.setData(nodeModel_.index(i), v, EditorNodeListModel::ConditionKeyRole);
            emit selectionChanged();
            return;
        }
    }
}

QString WorkflowEditorModel::selConditionEquals() const {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_)
            return nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::ConditionEqualsRole).toString();
    }
    return {};
}
void WorkflowEditorModel::setSelConditionEquals(const QString& v) {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_) {
            nodeModel_.setData(nodeModel_.index(i), v, EditorNodeListModel::ConditionEqualsRole);
            emit selectionChanged();
            return;
        }
    }
}

QString WorkflowEditorModel::selParallelGroup() const {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_)
            return nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::ParallelGroupIdRole).toString();
    }
    return {};
}
void WorkflowEditorModel::setSelParallelGroup(const QString& v) {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_) {
            nodeModel_.setData(nodeModel_.index(i), v, EditorNodeListModel::ParallelGroupIdRole);
            emit selectionChanged();
            return;
        }
    }
}

QString WorkflowEditorModel::selCompensationStep() const {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_)
            return nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::CompensationStepIdRole).toString();
    }
    return {};
}
void WorkflowEditorModel::setSelCompensationStep(const QString& v) {
    for (int i = 0; i < nodeModel_.rowCount(); ++i) {
        if (nodeModel_.data(nodeModel_.index(i), EditorNodeListModel::StepIdRole).toString() == selectedStepId_) {
            nodeModel_.setData(nodeModel_.index(i), v, EditorNodeListModel::CompensationStepIdRole);
            emit selectionChanged();
            return;
        }
    }
}

// 连线
QString WorkflowEditorModel::selSuccessTarget() const {
    for (const auto& c : connModel_.connections()) {
        if (c.fromStepId == selectedStepId_ && c.type == "success") return c.toStepId;
    }
    return {};
}
void WorkflowEditorModel::setSelSuccessTarget(const QString& id) {
    setTransition(selectedStepId_, id, "success");
    emit selectionChanged();
}
QString WorkflowEditorModel::selFailureTarget() const {
    for (const auto& c : connModel_.connections()) {
        if (c.fromStepId == selectedStepId_ && c.type == "failure") return c.toStepId;
    }
    return {};
}
void WorkflowEditorModel::setSelFailureTarget(const QString& id) {
    setTransition(selectedStepId_, id, "failure");
    emit selectionChanged();
}
QString WorkflowEditorModel::selSkippedTarget() const {
    for (const auto& c : connModel_.connections()) {
        if (c.fromStepId == selectedStepId_ && c.type == "skipped") return c.toStepId;
    }
    return {};
}
void WorkflowEditorModel::setSelSkippedTarget(const QString& id) {
    setTransition(selectedStepId_, id, "skipped");
    emit selectionChanged();
}

// --- 私有 ---

QString WorkflowEditorModel::nextNodeColor(int index) const {
    return kNodeColors.at(index % kNodeColors.size());
}

void WorkflowEditorModel::rebuildConnectionsFromNodes() {
    connModel_.clear();
    for (const auto& n : nodeModel_.nodes()) {
        // 节点属性中的 compensationStepId
        if (!n.compensationStepId.isEmpty()) {
            connModel_.addConnection({n.stepId, n.compensationStepId, "compensation"});
        }
    }
}

void WorkflowEditorModel::updateSelectedFromCurrent() {
    // 当节点模型数据变化时，刷新选中属性（由 connectionChanged 触发
}

} // namespace eon::studio
