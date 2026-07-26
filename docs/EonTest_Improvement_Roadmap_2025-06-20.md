# EonTest 对标 OpenTAP — 精准改善路线图

> 基于 2026-06-20 全模块代码审计
> 已确认已有：BreakOffered、preallocateResources+依赖分析、collectAnalyzerResults、并行组、loop检测、compensation、RetryPolicy、CapabilityRegistry、MatrixManager

---

## 现状评估：比想象中完善

经过本轮深度审计，以下功能**在之前报告中标记为"缺失"，实际已实现**：

| 功能 | OpenTAP | EonTest 现状 | 完成度 |
|------|---------|-------------|--------|
| **BreakOffered 暂停** | `BreakOffered` 事件 | `WorkflowEngine::onBreakOffered` 回调 + `ExecutionControl::Pause/Skip/Abort/Continue` | ✅ 已实现 |
| **预分配资源** | `ResourceTaskManager.OpenAll()` | `WorkflowEngine::preallocateResources()` + Tarjan 依赖分层 | ✅ 已实现 |
| **并行步骤** | `ParallelStep` 子线程 | `parallelGroupId` + `std::async` 线程池并行 | ✅ 已实现 |
| **Analyzer 收集** | `IResultListener.OnResultPublished` | `collectAnalyzerResults()` + mergeAnalyzerResults | ✅ 已实现 |
| **循环检测** | 无内置 | `stepVisitCount > 100` → fail | ✅ 已实现 |
| **补偿步骤** | 无内置 | `runCompensation()` 逆序执行 | ✅ EonTest 独有 |
| **重试策略** | 无内置 | `StepExecutionPolicy.maxRetries` | ✅ EonTest 独有 |
| **环境快照** | 无内置 | `captureSnapshot()` | ✅ EonTest 独有 |

### 真正缺失的 12 项（按实施难度+影响力排序）

| # | 功能 | 影响力 | 难度 | 优先级 |
|---|------|--------|------|--------|
| 1 | **DUT 生命周期注入** | ⭐⭐⭐⭐⭐ | ⭐⭐ | P0 |
| 2 | **StepResult 结构化发布** | ⭐⭐⭐⭐⭐ | ⭐⭐ | P0 |
| 3 | **Per-step BreakCondition** | ⭐⭐⭐⭐ | ⭐ | P0 |
| 4 | **ResourceOpen 属性** | ⭐⭐⭐ | ⭐⭐ | P1 |
| 5 | **Artifacts 管道** | ⭐⭐⭐⭐ | ⭐⭐⭐ | P1 |
| 6 | **声明式验证规则** | ⭐⭐⭐ | ⭐⭐ | P1 |
| 7 | **DutSettings 配置面板** | ⭐⭐⭐ | ⭐⭐⭐ | P1 |
| 8 | **Connection/Port 建模** | ⭐⭐ | ⭐⭐⭐⭐ | P2 |
| 9 | **Input\<T\>/Output 数据流** | ⭐⭐⭐ | ⭐⭐⭐ | P2 |
| 10 | **Expressions 表达式引擎** | ⭐⭐⭐ | ⭐⭐⭐⭐ | P2 |
| 11 | **Mixins 动态扩展** | ⭐⭐ | ⭐⭐⭐⭐⭐ | P2 |
| 12 | **ExternalParameters CLI** | ⭐⭐ | ⭐⭐ | P2 |

---

## P0-1: DUT 生命周期注入（影响力最大，难度最低）

### 问题诊断

```cpp
// SDK/include/eon/sdk/WorkflowContext.h — 字段已存在但从未赋值！
struct WorkflowContext {
    IDut* dut = nullptr;  // ← 永远是 nullptr
};

// Runtime/src/WorkflowEngine.cpp:executeWorkflowWithParams()
eon::sdk::WorkflowContext context;
context.workflowId = workflow.workflowId;
context.data = workflow.initialData;
context.resourceManager = resourceManager_; // ✅ 已注入
// context.dut = ??? — ❌ 从未赋值！

// ScpiTraceEvent 有 dutId 字段但 writeScpiTrace 不传
event.dutId; // ← 永远是空字符串
```

### 改造方案（3 处修改，约 50 行代码）

#### Step 1: WorkflowDefinition 增加 dut 绑定

```cpp
// Domain/include/eon/domain/WorkflowDefinition.h
struct WorkflowDefinition {
    QString workflowId;
    QString entryStepId;
    QVariantMap initialData;
    QList<ActivityStep> steps;
    
    // +++ 新增 +++
    QString dutPluginId;  // DUT 插件 ID（如 "simple.dut"）
    QVariantMap dutConfig; // DUT 配置（如序列号、端口）
};
```

#### Step 2: PluginManager 增加 DUT 插件发现

```cpp
// Runtime/include/eon/runtime/PluginManager.h
class PluginManager {
public:
    // +++ 新增 +++
    eon::sdk::IDut* findDutPluginById(const QString& pluginId) const;
    QHash<QString, eon::sdk::IDut*> dutPlugins() const { return dutPlugins_; }
    
private:
    QHash<QString, eon::sdk::IDut*> dutPlugins_;
};

// Runtime/src/PluginManager.cpp — 加载时同时扫描 IDut
bool PluginManager::loadPlugins(const QString& dir, QString* error) {
    // ... existing step/analyzer/reporter loading ...
    
    // +++ 新增：加载 DUT 插件 +++
    for (auto* plugin : allPlugins) {
        if (auto* dut = qobject_cast<eon::sdk::IDut*>(plugin)) {
            dutPlugins_.insert(dut->dutId(), dut);
        }
    }
}
```

#### Step 3: WorkflowEngine 注入 DUT 到 context

```cpp
// Runtime/src/WorkflowEngine.cpp:executeWorkflowWithParams()
// 在 context 构造后、executeStep 前插入：

// +++ 新增：从 WorkflowDefinition 加载 DUT +++
if (!workflow.dutPluginId.isEmpty()) {
    auto* dut = pluginManager_.findDutPluginById(workflow.dutPluginId);
    if (dut) {
        // 配置 DUT
        // dut->setDutId(workflow.dutConfig.value("serialNumber").toString());
        context.dut = dut;  // ← 关键：注入到 context
        
        // 如果 DUT 也是 IResource，注册到 ResourceManager
        if (auto* res = dynamic_cast<eon::sdk::IResource*>(dut)) {
            resourceManager_->registerResource(workflow.dutPluginId.toStdString(), res);
        }
    }
}

// +++ 修改 writeScpiTrace：自动从 context.dut 填充 dutId +++
void WorkflowEngine::writeScpiTrace(..., const eon::sdk::WorkflowContext& ctx) {
    event.dutId = ctx.dut ? ctx.dut->dutId() : QString();  // ← 自动填充
    // ...
}

// +++ 修改 step-results.jsonl：写入 dutId +++
stepResult["dutId"] = context.dut ? context.dut->dutId() : QString();
```

#### Step 4: 创建示例 DUT 插件

```cpp
// Plugins/SimpleDut/SimpleDutPlugin.h
class SimpleDutPlugin : public eon::sdk::Dut {
    Q_OBJECT
    Q_INTERFACES(eon::sdk::IDut)
public:
    QString id() const override { return "simple.dut"; }
    
    bool open() override {
        // 通过串口/TCP 连接 DUT，发送初始化命令
        connected_ = io_->open(config_);
        return connected_;
    }
    
    void close() override {
        io_->close();
        connected_ = false;
    }
};
```

---

## P0-2: StepResult 结构化发布（对标 OpenTAP ResultTable）

### 问题诊断

当前步骤结果散落在 `context.data` 的 QVariantMap 中，没有统一的表格抽象：

```cpp
// 当前写法：各插件自由发挥
context.data.insert("measuredValue", 3.14);
context.data.insert("measuredUnit", "V");
// 没有列定义、没有多行数据、无法高效批量输出
```

### 改造方案：新增 StepResult 结构化类型

#### Step 1: 定义 ResultColumn + ResultTable

```cpp
// SDK/include/eon/sdk/StepResult.h — 新文件
#pragma once
#include <QString>
#include <QVector>
#include <QVariant>
#include <QVariantMap>

namespace eon::sdk {

/// 单列结果数据（对标 OpenTAP ResultColumn）
struct ResultColumn {
    QString name;           // 列名，如 "Frequency [Hz]"
    QVector<double> values; // 列数据
    QString unit;           // 单位（可选）
};

/// 结构化结果表（对标 OpenTAP ResultTable）
struct ResultTable {
    QString name;                       // 表名，如 "Sweep Results"
    QVector<ResultColumn> columns;      // N 列
    int rowCount() const { 
        return columns.isEmpty() ? 0 : columns[0].values.size(); 
    }
    
    // 便捷构造：单行结果
    static ResultTable singleRow(
        const QString& tableName,
        const QVector<QPair<QString, double>>& nameValuePairs);
    
    // 便捷构造：多列多行
    static ResultTable fromColumns(
        const QString& tableName,
        std::initializer_list<ResultColumn> cols);
    
    QVariantMap toVariantMap() const;
};

/// 每个步骤可发布 0-N 个 ResultTable
struct StepResult {
    QString stepId;
    QString pluginId;
    QVector<ResultTable> tables;   // 0-N 张结果表
    QStringList artifactPaths;     // 关联的 artifact 文件
    QVariantMap metadata;          // 元数据（dutId, cellId 等）
    
    QVariantMap toVariantMap() const;
};

} // namespace eon::sdk
```

#### Step 2: 插件使用示例

```cpp
// Plugins/Multimeter/MultimeterPlugin.cpp
bool MultimeterPlugin::executeStep(WorkflowContext& ctx, QString& err) {
    double voltage = measureVoltage();
    
    // 新版：发布结构化结果
    auto table = ResultTable::singleRow("DC Measurement", {
        {"Voltage [V]", voltage},
        {"Current [A]", measureCurrent()}
    });
    
    ctx.data.insert("_stepResult", QVariant::fromValue(table.toVariantMap()));
    return true;
}

// 批量测量（对标 OpenTAP PublishTable）
bool SweepPlugin::executeStep(WorkflowContext& ctx, QString& err) {
    QVector<double> freqs, powers;
    for (double f = 1e6; f <= 1e9; f *= 2) {
        freqs.append(f);
        powers.append(measurePowerAt(f));
    }
    
    auto table = ResultTable::fromColumns("Power vs Frequency", {
        {"Frequency [Hz]", freqs},
        {"Power [dBm]", powers}
    });
    
    ctx.data.insert("_stepResult", QVariant::fromValue(table.toVariantMap()));
    return true;
}
```

#### Step 3: WorkflowEngine 统一收集

```cpp
// Runtime/src/WorkflowEngine.cpp — runStep lambda 中
if (pluginSucceeded && !timedOut) {
    // +++ 新增：提取 StepResult 并写入 step-results.jsonl +++
    if (context.data.contains("_stepResult")) {
        QJsonObject stepResult; /* ... existing fields ... */
        
        // 写入结构化表格
        auto tableMap = context.data.value("_stepResult").toMap();
        stepResult["resultTable"] = QJsonObject::fromVariantMap(tableMap);
        
        // 确保 dutId 被填充
        stepResult["dutId"] = context.dut ? context.dut->dutId() : QString();
        
        // ...
    }
}
```

---

## P0-3: Per-step BreakCondition（对标 OpenTAP BreakCondition）

### 问题诊断

`WorkflowEngine::onBreakOffered` 回调已存在但它是全局的——每个步骤行为一致。OpenTAP 支持**每步骤独立配置**何时中断：

```csharp
// OpenTAP: 每个步骤独立配置
step.BreakCondition = BreakCondition.BreakOnFail | BreakCondition.BreakOnError;
```

### 改造方案（2 处修改，约 30 行代码）

#### Step 1: ActivityStep 增加 breakCondition

```cpp
// Domain/include/eon/domain/WorkflowDefinition.h

// +++ 新增枚举 +++
enum class BreakCondition {
    Inherit = 1,        // 继承引擎默认
    BreakOnError = 2,   // Error 时中断
    BreakOnFail = 4,    // Fail 时中断
    BreakOnInconclusive = 8, // Inconclusive 时中断
};

struct ActivityStep {
    // ... 现有字段 ...
    
    // +++ 新增 +++
    int breakCondition = 1; // 默认 Inherit
};
```

#### Step 2: WorkflowEngine 应用 breakCondition

```cpp
// Runtime/src/WorkflowEngine.cpp — runStep lambda 的 switch 中
// 在判定 stepRunOutcome 之后、transition 之前插入：

// +++ 新增：根据 breakCondition 决定是否中断 +++
auto shouldBreak = [&](StepRunOutcome outcome, const ActivityStep* step) -> bool {
    int cond = step->breakCondition;
    if (cond == static_cast<int>(BreakCondition::Inherit))
        return false; // 继承模式：永不自动中断
    
    if (outcome == StepRunOutcome::HardFailed && (cond & 2))
        return true;
    if (outcome == StepRunOutcome::ContinueOnError && (cond & 2))
        return true;
    // 注意：EonTest 不区分 Inconclusive/Fail/Error 的 StepRunOutcome
    // 需要从 finalVerdict_ 中获取更细粒度的判断
    return false;
};

if (shouldBreak(stepRunOutcome, step)) {
    currentStepId.clear(); // 中断后续步骤
    break; // 跳出 while 循环
}
```

---

## P1-4: ResourceOpen 属性系统

### 设计思路

利用现有的 `IResource::dependencies()` 接口，增加打开策略：

```cpp
// SDK/include/eon/sdk/IResource.h
enum class ResourceOpenPolicy {
    Before,      // 依赖先打开（默认）
    InParallel,  // 与依赖并行打开
    Ignore       // 不自动打开依赖
};

class IResource {
public:
    // 现有
    virtual std::vector<IResource*> dependencies() const { return {}; }
    
    // +++ 新增：返回每个依赖的打开策略 +++
    virtual ResourceOpenPolicy dependencyPolicy(IResource* dep) const {
        return ResourceOpenPolicy::Before; // 默认串行
    }
};
```

`ResourceDependencyAnalyzer::analyze()` 中根据 `dependencyPolicy()` 决定将依赖放入 `strongDependencies`（Before）还是 `weakDependencies`（Parallel），或者完全忽略。

---

## P1-5: Artifacts 管道（对标 OpenTAP IArtifactListener）

### 设计思路

```cpp
// SDK/include/eon/sdk/IArtifactListener.h — 新文件
namespace eon::sdk {

/// Artifact 发布者接口（步骤/报告器实现）
class IArtifactPublisher {
public:
    virtual ~IArtifactPublisher() = default;
    /// 发布文件 artifact
    virtual void publishArtifact(const QString& filePath, const QString& mimeType = {}) = 0;
    /// 发布流 artifact
    virtual void publishArtifact(QIODevice* stream, const QString& name) = 0;
};

/// Artifact 监听器接口（结果收集器实现）
class IArtifactListener {
public:
    virtual ~IArtifactListener() = default;
    /// 收到 artifact 通知
    virtual void onArtifactPublished(const QString& artifactPath,
                                      const QString& mimeType,
                                      const QString& sourceStepId) = 0;
};

}
```

WorkflowEngine 作为中介：步骤通过 `context.publisher->publishArtifact()` 发布 → Engine 转发给所有 `IArtifactListener`。

---

## P1-6: 声明式验证规则（对标 OpenTAP Rules）

```cpp
// SDK/include/eon/sdk/ValidationRule.h — 新文件
namespace eon::sdk {

class ValidationRule {
public:
    using CheckFunc = std::function<bool()>;
    
    ValidationRule(CheckFunc check, QString message, QStringList affectedProperties)
        : check_(std::move(check))
        , message_(std::move(message))
        , affectedProperties_(std::move(affectedProperties)) {}
    
    bool validate(QString* errorMessage = nullptr) const {
        bool ok = check_();
        if (!ok && errorMessage) *errorMessage = message_;
        return ok;
    }
    
    QString message() const { return message_; }
    QStringList affectedProperties() const { return affectedProperties_; }
    
private:
    CheckFunc check_;
    QString message_;
    QStringList affectedProperties_;
};

} // namespace eon::sdk
```

IStepPlugin 增加虚方法：

```cpp
class IStepPlugin : public IPlugin {
public:
    // 现有接口...
    
    // +++ 新增 +++
    virtual std::vector<ValidationRule> validationRules() const { return {}; }
};
```

Studio 在属性面板更新时调用 `validationRules()`，失败的在对应属性旁显示红色警告。

---

## P1-7: DutSettings / InstrumentSettings Bench 面板

对标 OpenTAP 的 `ComponentSettingsList<T>` 模式：

```
Settings > Bench
  ├── DUTs         — DutSettings (可编辑 DUT 列表)
  ├── Instruments   — InstrumentSettings (可编辑仪器列表)
  └── Connections   — ConnectionSettings
```

EonTest 已有 `Recipe` 系统（SQLite 持久化参数模板），可以扩展为：

```cpp
// Studio 侧：QML BenchSettingsPanel
// 数据来源：JSON 配置文件 bench.json
{
  "duts": [
    {"pluginId": "simple.dut", "dutId": "SN-001", "model": "E36313A", "port": "COM5"},
    {"pluginId": "simple.dut", "dutId": "SN-002", "model": "E36313A", "port": "COM6"}
  ],
  "instruments": [
    {"pluginId": "dmm.plugin", "visaAddress": "TCPIP::192.168.1.10::INSTR"},
    {"pluginId": "power.plugin", "visaAddress": "TCPIP::192.168.1.11::INSTR"}
  ]
}
```

WorkflowDefinition 引用 DUT/Instrument 时用 ID 而非直接写连接串：

```json
{
  "dutPluginId": "SN-001",
  "steps": [
    {
      "stepId": "step.measure",
      "pluginId": "dmm.plugin",
      "initialData": {
        "instrumentRef": "dmm.plugin"  // 引用 bench 配置中的仪器
      }
    }
  ]
}
```

---

## P2 项：简要设计要点

### P2-8: Connection/Port 物理建模
- EonTest 已有 `MatrixManager`（动态交换路由），OpenTAP 有 `Connection/Port`（静态物理连线）
- 两者互补：Matrix 管理动态路由，Connection 建模静态线缆（含损耗补偿）
- 建议在 IResource 上增加 `ports()` 虚方法，复用 MatrixManager 的 `route()` 原语

### P2-9: Input\<T\>/Output 类型安全数据流
- 当前 `context.data` (QVariantMap) 是无类型的
- 新增模板类 `Input<T>` + `Output<T>`，编译期检查类型
- Studio 增加"连线"可视化：右键输出 → Assign to Input → 选择目标步骤

### P2-10: Expressions 表达式引擎
- 独立模块 `SDK/src/ExpressionEngine.cpp`
- 支持：算术 + 三角函数 + 字符串插值 + `@stepId.property` 引用
- 在 steps 的 `initialData` 中支持：`"delay": "=10 * 60"` → 解析为 600

### P2-11: Mixins 动态扩展
- C++ 中可以通过 QMetaObject 动态属性 + CRTP 实现类似效果
- 本质上是对 `EmbedPropertiesAttribute` + `IMixinBuilder` 的 C++ 翻译
- 需定义 `IMixinBuilder` 接口 + `MixinMemberData` 动态成员描述

### P2-12: ExternalParameters CLI
- 对标 `tap run -e "Frequency=10MHz"`
- WorkflowEngine 增加 `setExternalParameter(key, value)` 
- RuntimeWorker main() 解析 `--external` 参数

---

## 实施总结

```
P0（1-2 周，立即可做，影响最大）：
  ✅ DUT 生命周期注入 — 3 处修改，~50 行新代码
  ✅ StepResult 结构化发布 — 1 个新头文件 + 2 处集成
  ✅ Per-step BreakCondition — 1 个枚举 + 1 个 lambda

P1（2-4 周，提升健壮性）：
  ✅ ResourceOpen 属性 — 接口扩展
  ✅ Artifacts 管道 — 新接口 + 引擎集成
  ✅ 声明式验证规则 — 新类型 + Studio UI
  ✅ DutSettings Bench 面板 — JSON 配置 + QML 面板

P2（4-8 周，差异化竞争力）：
  ✅ Connection/Port 建模 — 与 Matrix 互补
  ✅ Input<T>/Output — 类型安全数据流
  ✅ Expressions — 独立表达式引擎模块
  ✅ Mixins — QMetaObject 动态扩展
  ✅ ExternalParameters — CLI 参数化
```

**关键发现**：EonTest 的 `WorkflowEngine` 已经是一个非常完善的执行引擎（BreakOffered、依赖分析、并行组、loop 检测、compensation），主要差距在**概念深度集成**（DUT 未贯穿生命周期）和**结构化能力**（缺少 ResultTable/Artifact 抽象），而非基础执行能力。
