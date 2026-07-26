#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QStringList>

namespace eon::studio {

// ============================================================================
// EditorNode — 画布上的节点（QML 可绑定）
// ============================================================================
struct EditorNode {
    QString stepId;
    QString pluginId;
    QString label;              // 显示标签
    qreal x = 0;                // 画布 X 坐标
    qreal y = 0;                // 画布 Y 坐标
    QString color;              // 节点颜色

    // 步骤属性
    int maxRetries = 0;
    int timeoutMs = 0;
    QString failurePolicy = "fail_fast";
    QString parallelGroupId;
    QString conditionKey;
    QString conditionEquals;
    QString compensationStepId;

    bool operator==(const EditorNode& o) const { return stepId == o.stepId; }
};

// ============================================================================
// EditorConnection — 节点间连线
// ============================================================================
struct EditorConnection {
    QString fromStepId;
    QString toStepId;
    QString type;               // "success" | "failure" | "skipped" | "compensation"
};

// ============================================================================
// EditorNodeListModel — 节点列表 QAbstractListModel
// ============================================================================
class EditorNodeListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        StepIdRole = Qt::UserRole + 1,
        PluginIdRole, LabelRole, XRole, YRole, ColorRole,
        MaxRetriesRole, TimeoutMsRole, FailurePolicyRole,
        ParallelGroupIdRole, ConditionKeyRole, ConditionEqualsRole,
        CompensationStepIdRole
    };

    explicit EditorNodeListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int count() const { return static_cast<int>(nodes_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    /// 便捷获取（QML 侧：model.get(index) 或 model.data(index, role) 均可用）
    Q_INVOKABLE QVariant get(int index) const;

    void addNode(const EditorNode& node);
    void removeNode(const QString& stepId);
    void updateNodePosition(const QString& stepId, qreal x, qreal y);
    void clear();

    EditorNode* findNode(const QString& stepId);
    const QList<EditorNode>& nodes() const { return nodes_; }

signals:
    void countChanged();

private:
    QList<EditorNode> nodes_;
};

// ============================================================================
// EditorConnectionListModel — 连线列表模型
// ============================================================================
class EditorConnectionListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        FromStepIdRole = Qt::UserRole + 1,
        ToStepIdRole, TypeRole
    };

    explicit EditorConnectionListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int count() const { return static_cast<int>(connections_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 便捷获取（QML 侧）
    Q_INVOKABLE QVariant get(int index) const;

    void addConnection(const EditorConnection& conn);
    void removeConnectionsFrom(const QString& stepId);
    void removeConnectionsTo(const QString& stepId);
    void removeConnectionsByType(const QString& fromStepId, const QString& type);
    void clear();

    const QList<EditorConnection>& connections() const { return connections_; }

signals:
    void countChanged();

private:
    QList<EditorConnection> connections_;
};

// ============================================================================
// WorkflowEditorModel — 工作流编辑器主模型
//
// 持有可编辑的工作流，管理节点和连线模型。
// 暴露 QML 可绑定的属性用于属性面板。
// ============================================================================
class WorkflowEditorModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString workflowId READ workflowId WRITE setWorkflowId NOTIFY workflowIdChanged)
    Q_PROPERTY(int priority READ priority WRITE setPriority NOTIFY priorityChanged)
    Q_PROPERTY(QString entryStepId READ entryStepId WRITE setEntryStepId NOTIFY entryStepIdChanged)
    Q_PROPERTY(QObject* nodes READ nodes CONSTANT)
    Q_PROPERTY(QObject* connections READ connections CONSTANT)
    Q_PROPERTY(int nodeCount READ nodeCount NOTIFY statusTextChanged)
    Q_PROPERTY(int connCount READ connCount NOTIFY statusTextChanged)
    Q_PROPERTY(QStringList availablePlugins READ availablePlugins NOTIFY availablePluginsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

    // 当前选中节点的属性（属性面板绑定）
    Q_PROPERTY(QString selStepId READ selStepId NOTIFY selectionChanged)
    Q_PROPERTY(QString selPluginId READ selPluginId WRITE setSelPluginId NOTIFY selectionChanged)
    Q_PROPERTY(int selMaxRetries READ selMaxRetries WRITE setSelMaxRetries NOTIFY selectionChanged)
    Q_PROPERTY(int selTimeoutMs READ selTimeoutMs WRITE setSelTimeoutMs NOTIFY selectionChanged)
    Q_PROPERTY(QString selFailurePolicy READ selFailurePolicy WRITE setSelFailurePolicy NOTIFY selectionChanged)
    Q_PROPERTY(QString selConditionKey READ selConditionKey WRITE setSelConditionKey NOTIFY selectionChanged)
    Q_PROPERTY(QString selConditionEquals READ selConditionEquals WRITE setSelConditionEquals NOTIFY selectionChanged)
    Q_PROPERTY(QString selParallelGroup READ selParallelGroup WRITE setSelParallelGroup NOTIFY selectionChanged)
    Q_PROPERTY(QString selCompensationStep READ selCompensationStep WRITE setSelCompensationStep NOTIFY selectionChanged)
    Q_PROPERTY(QString selSuccessTarget READ selSuccessTarget WRITE setSelSuccessTarget NOTIFY selectionChanged)
    Q_PROPERTY(QString selFailureTarget READ selFailureTarget WRITE setSelFailureTarget NOTIFY selectionChanged)
    Q_PROPERTY(QString selSkippedTarget READ selSkippedTarget WRITE setSelSkippedTarget NOTIFY selectionChanged)

public:
    explicit WorkflowEditorModel(QObject* parent = nullptr);

    // --- 工作流属性 ---
    QString workflowId() const;
    void setWorkflowId(const QString& id);
    int priority() const;
    void setPriority(int p);
    QString entryStepId() const;
    void setEntryStepId(const QString& id);

    // --- 子模型 ---
    QObject* nodes() { return &nodeModel_; }
    QObject* connections() { return &connModel_; }
    int nodeCount() const { return nodeModel_.count(); }
    int connCount() const { return connModel_.count(); }

    // --- 可用插件 ---
    QStringList availablePlugins() const;
    void setAvailablePlugins(const QStringList& plugins);

    QString statusText() const;

    // --- 选中节点属性访问器 ---
    QString selStepId() const;
    QString selPluginId() const;       void setSelPluginId(const QString& id);
    int selMaxRetries() const;          void setSelMaxRetries(int r);
    int selTimeoutMs() const;           void setSelTimeoutMs(int ms);
    QString selFailurePolicy() const;    void setSelFailurePolicy(const QString& p);
    QString selConditionKey() const;     void setSelConditionKey(const QString& k);
    QString selConditionEquals() const;  void setSelConditionEquals(const QString& v);
    QString selParallelGroup() const;    void setSelParallelGroup(const QString& g);
    QString selCompensationStep() const; void setSelCompensationStep(const QString& s);
    QString selSuccessTarget() const;    void setSelSuccessTarget(const QString& id);
    QString selFailureTarget() const;    void setSelFailureTarget(const QString& id);
    QString selSkippedTarget() const;    void setSelSkippedTarget(const QString& id);

public slots:
    // --- 节点操作 ---
    /// 添加节点到画布
    Q_INVOKABLE void addNode(const QString& stepId, const QString& pluginId, qreal x, qreal y);

    /// 删除节点
    Q_INVOKABLE void removeNode(const QString& stepId);

    /// 移动节点
    Q_INVOKABLE void moveNode(const QString& stepId, qreal x, qreal y);

    /// 选中节点
    Q_INVOKABLE void selectNode(const QString& stepId);

    /// 取消选中
    Q_INVOKABLE void deselectNode();

    /// 设置节点间连线（自动创建/删除 EditorConnection）
    Q_INVOKABLE void setTransition(const QString& fromStepId, const QString& toStepId, const QString& type);

    /// 删除连线
    Q_INVOKABLE void removeTransition(const QString& fromStepId, const QString& type);

    // --- 文件操作 ---
    /// 从 JSON 加载工作流
    Q_INVOKABLE bool loadFromJson(const QString& jsonText);

    /// 从文件加载工作流 JSON
    Q_INVOKABLE bool loadFromFile(const QString& filePath);

    /// 导出为 JSON
    Q_INVOKABLE QString toJson() const;

    /// 新建空工作流
    Q_INVOKABLE void newWorkflow(const QString& wfId = "new-workflow");

    /// 获取入口步骤 ID（第一个没有入边的步骤）
    Q_INVOKABLE QString autoDetectEntryStep() const;

signals:
    void workflowIdChanged();
    void priorityChanged();
    void entryStepIdChanged();
    void availablePluginsChanged();
    void statusTextChanged();
    void selectionChanged();
    void nodeAdded(const QString& stepId);
    void nodeRemoved(const QString& stepId);
    void connectionChanged();

private:
    QString nextNodeColor(int index) const;
    void rebuildConnectionsFromNodes();
    void updateSelectedFromCurrent();

    EditorNodeListModel nodeModel_;
    EditorConnectionListModel connModel_;

    QString workflowId_ = "new-workflow";
    int priority_ = 0;
    QString entryStepId_;
    QStringList availablePlugins_;
    QString selectedStepId_;

    int nodeCounter_ = 0;

    static const QStringList kNodeColors;
};

} // namespace eon::studio
