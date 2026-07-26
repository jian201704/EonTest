# OpenTAP vs EonTest — 深度学习与全面对比分析

> 基于 OpenTAP 9.33.0 源码 + 完整文档 + EonTest 全模块审计
> 日期：2026-06-20

---

## 一、关于 DUT 的实现现状

### 1.1 纠正之前的判断

**之前的判断不准确。** 经过本轮全模块审计，DUT 在 EonTest 中**已有接口定义和基类实现**：

```
SDK/include/eon/sdk/IDut.h     — IDut : IResource 接口（dutId/modelName/firmwareVersion/description）
SDK/include/eon/sdk/Dut.h      — Dut : IDut 基类（默认 open/close 实现）
SDK/src/Dut.cpp                — Dut 构造函数 + open()/close() 默认空实现
SDK/include/eon/sdk/WorkflowContext.h — WorkflowContext 中有 IDut* dut 字段
Domain/include/eon/domain/TestResult.h — TestResult 有 dutId_ 字段
Studio/src/StudioBackend.cpp   — 日志面板显示 [DUT:xxx]
```

### 1.2 但 DUT 确实没有"吃透"

虽然接口/基类存在，但 OpenTAP 的 DUT 远不止一个接口——它是一个**深度集成到整个引擎生命周期的第一等公民**。EonTest 当前的状态相当于只画了骨架，但肌肉、神经、血液循环都没有：

| 维度 | OpenTAP DUT | EonTest DUT |
|------|------------|-------------|
| **接口定义** | `IDut : IResource` | `IDut : IResource` ✅ |
| **基类属性** | Name, ID, Comment, IsConnected | dutId, modelName, firmwareVersion, description ✅ |
| **Bench 配置面板** | `DutSettings` (Settings > Bench > DUTs) 带 Profile | ❌ 无 GUI 配置面板 |
| **自动资源发现** | 引擎扫描所有 IDut 插件 → Bench 列表 | ❌ 需手动在代码中创建 |
| **自动注入步骤** | 步骤声明 `public Dut MyDut { get; set; }` → 引擎自动绑定 | WorkflowContext 有 dut 字段但未在 RuntimeWorker 赋值 |
| **结果元数据绑定** | `[MetaData(true, "DUT ID", Group="DUT")]` 自动标记 | TestResult 有 dutId_ 字段但未从 DUT 对象自动填充 |
| **Open/Close 生命周期** | `ResourceTaskManager` 自动异步并行 Open/Close | ResourceManager 支持但 DUT 未注册进依赖分析 |
| **资源依赖** | DUT 可作为仪器依赖源 (ResourceOpen: Before/Parallel/Ignore) | dependencies() 已定义但未实际使用 |
| **多 DUT 并行测试** | 原生支持（步骤可持有多个 DUT 属性） | ❌ 无 |
| **CLI 参数化** | `--metadata dut-id=5` | ❌ 无 |
| **物理连接建模** | Connection/Port 模型连接 DUT↔Instrument | ❌ 无 |

### 1.3 结论

**"DUT 没实现"这个判断是对了一半的。** 接口骨架有了，但以下关键集成点完全是空的：

1. **没有 DUT 插件实例** — `Plugins/` 目录下没有一个实现 `IDut` 的插件
2. **WorkflowContext.dut 从未被赋值** — RuntimeWorker 中没有从配置加载 DUT 并注入到 context 的代码
3. **没有 Bench 配置 UI** — 没有等价于 OpenTAP `DutSettings` 的 GUI 面板
4. **没有 DUT↔结果自动绑定** — TestResult.dutId_ 存在但无人填充

---

## 二、OpenTAP 的核心优势（EonTest 应深度学习）

### 2.1 DUT 作为"第一等公民"的深度集成

OpenTAP 的 DUT 不是简单的接口定义，而是贯穿整个引擎的"垂直概念"：

```
┌─────────────────────────────────────────────────────┐
│                    OpenTAP DUT 完整集成链路            │
├─────────────────────────────────────────────────────┤
│                                                       │
│  1. 插件发现: PluginManager → 扫描所有 IDut 实现       │
│       ↓                                               │
│  2. Bench 配置: DutSettings (ComponentSettingsList)    │
│      用户可添加/删除/配置 DUT 实例（名称、ID、备注）    │
│      支持 Profile 切换（不同产线/工位用不同 DUT 配置）  │
│       ↓                                               │
│  3. 步骤声明: TestStep 中声明 DUT 属性                  │
│      public Dut MyDut { get; set; }                   │
│      → 引擎通过 ResourceDependencyAnalyzer 自动发现    │
│       ↓                                               │
│  4. 执行前 Open: ResourceTaskManager 依赖分析 + 异步打开 │
│      → 按 ResourceOpen 属性决定开/并行/忽略             │
│      → 步骤 PrePlanRun 前确保所有 DUT 已 Open          │
│       ↓                                               │
│  5. 执行中使用: 步骤 Run() 中通过 MyDut 属性直接访问    │
│      → ID/Name/Comment 通过 [MetaData] 自动写入结果    │
│       ↓                                               │
│  6. 执行后 Close: ResourceTaskManager 自动关闭          │
│       ↓                                               │
│  7. 结果追溯: MetaData("DUT", "DUT ID") → 结果永久绑定  │
│                                                       │
└─────────────────────────────────────────────────────┘
```

**EonTest 缺失的关键环节**：
- 没有 PluginManager 扫描 IDut 实现并填充到配置面板
- 没有 `DutSettings` 等价物（Bench 配置持久化）
- 没有步骤属性声明 → 引擎自动绑定的反射机制
- 没有 `[MetaData]` 注解机制将 DUT 属性自动附加到结果

### 2.2 ResourceOpen 属性系统

这是 OpenTAP 最精巧的设计之一，控制资源依赖的打开方式：

```csharp
// Engine/ResourceTaskManager.cs
public enum ResourceOpenBehavior
{
    Before,     // 默认：依赖资源先 Open，再 Open 当前资源
    InParallel, // 并行：依赖资源和当前资源同时 Open
    Ignore      // 忽略：不自动 Open 该依赖资源
}
```

```csharp
// 使用示例：仪器依赖 DUT
public class MyInstrument : Instrument
{
    [ResourceOpen(ResourceOpenBehavior.Before)]  // DUT 先 Open
    public MyDut Dut { get; set; }
    
    [ResourceOpen(ResourceOpenBehavior.InParallel)] // 辅助仪器并行 Open
    public PowerSupply AuxPower { get; set; }
}
```

**依赖关系图 → Tarjan 拓扑排序 → 分层并行 Open**：

```
ResourceDependencyAnalyzer
  ├── 扫描所有步骤/资源的属性
  ├── 构建 IResource 强依赖图 (Before) / 弱依赖图 (Parallel)
  ├── Tarjan SCC 检测循环依赖
  └── 按拓扑序分层并行 Open
```

**EonTest 对应状态**：`IResource::dependencies()` 接口已定义但未被引擎实际使用。

### 2.3 双资源策略：Default vs Short Lived Connections

这是 OpenTAP 生产环境非常实用的特性：

| 策略 | 行为 | 适用场景 |
|------|------|---------|
| **Default Resource Manager** | Open → 整个 Plan 执行 → Close | 需要保持连接状态的长时间测试 |
| **Short Lived Connections** | 每个步骤执行完立即 Close | 避免连接泄漏、减少资源占用 |

```csharp
// Engine/ResourceTaskManager.cs (Default)
OpenAll() → Execute(step1, step2, ...) → CloseAll()

// Engine/LazyResourceManager.cs (Short Lived)
Open(step1) → Execute(step1) → Close(step1)
Open(step2) → Execute(step2) → Close(step2)
...
```

**EonTest 对比**：EonTest 的 ResourceManager 有 Lease/Heartbeat/Deadlock/Preemption，功能更丰富（工业级），但没有这种双策略模式。

### 2.4 Mixins 系统 — 运行时动态扩展

这是 OpenTAP 最值得 EonTest 学习的架构模式之一。Mixins 允许**不修改步骤代码**而动态添加功能：

```
用户右键设置 → "Add Mixin" → 选择 Mixin 类型 → 步骤即时获得新属性/行为
```

```csharp
// Mixin 类型
ITestStepPreRunMixin   → 步骤 Run() 前执行
ITestStepPostRunMixin  → 步骤 Run() 后执行  
ITestPlanPreRunMixin   → Plan 执行前执行一次

// 示例：Limit Check Mixin（不修改原步骤代码）
class LimitCheckMixin : ITestStepPostRunMixin {
    public double LowerLimit { get; set; }
    public double UpperLimit { get; set; }
    public void OnPostRun(TestStepPostRunEventArgs e) {
        // 检查步骤输出是否在限制内，自动升级 Verdict
    }
}
```

**关键机制**：`EmbedPropertiesAttribute` + `MixinMemberData` + `IMixinBuilder` 形成了一套**元编程框架**，在运行时动态修改类型成员。这在 C++/Qt 中可以用 QMetaObject/Q_PROPERTY 系统实现类似效果。

**EonTest 对比**：完全不支持。如果要实现类似效果，需要学习 `QMetaObject` 动态属性系统。

### 2.5 BreakCondition / BreakOffered — 执行控制

```csharp
// 每个步骤可设置的断点条件（Flags 组合）
public enum BreakCondition {
    Inherit = 1,        // 继承父级或引擎设置
    BreakOnError = 2,   // Error 时停止
    BreakOnFail = 4,    // Fail 时停止
    BreakOnInconclusive = 8,  // Inconclusive 时停止
    BreakOnPass = 16    // Pass 时停止（调试用）
}
```

`BreakOffered` 事件允许外部订阅者在每个步骤执行前**暂停/跳过/继续/中止**：

```csharp
// GUI 可以订阅此事件实现"单步调试"
plan.BreakOffered += (sender, args) => {
    // 显示对话框：继续 / 跳过 / 中止
    // args.TestStepRun.SuggestedNextStep 可跳转到指定步骤
};
```

**EonTest 对比**：完全缺失。只有步骤级的 `onSuccessStepId / onFailureStepId` 跳转。

### 2.6 Verdict 子步骤传播与覆盖

OpenTAP 的 Verdict 传播逻辑非常精细：

```
子步骤 Verdict: [Pass, Fail, Pass]
  → RunChildSteps() 自动取最严重 → 父步骤 = Fail

父步骤可覆盖:
  RunChildSteps(throwOnBreak: false);
  Verdict = Verdict.Pass; // 强制覆盖为 Pass
```

```csharp
// UpgradeVerdict 的线程安全实现（双重检查锁）
public void UpgradeVerdict(Verdict verdict) {
    if (Verdict < verdict) {  // 快速路径，避免锁竞争
        lock (upgradeVerdictLock) {
            if (Verdict < verdict)
                Verdict = verdict;
        }
    }
}
```

**EonTest 对应**：`Verdict.h` 已有完全等价的 `mergeVerdicts/upgradeVerdict`，对齐程度 100%。但 Verdict 传播到父步骤的逻辑未实现（因为没有步骤嵌套）。

### 2.7 ResultTable — 结构化结果发布

```csharp
// 单行结果
Results.Publish("PowerMeasurement", 
    new List<string>{"Frequency [Hz]", "Power [dBm]"}, 
    1e9, -10.5);

// 多行表格（最高效）
Results.PublishTable("Sweep Results",
    new List<string>{"Freq [Hz]", "Power [dBm]", "Phase [deg]"},
    frequencies,   // double[] 列1
    powers,        // double[] 列2
    phases);       // double[] 列3
```

ResultTable → IResultListener.OnResultPublished() → CSV/JSON/Binary 多种输出。

**EonTest 对比**：有 `AnalyzerResult` (QVariantMap) 但没有 `ResultTable` 的表格抽象。大量测量数据的列式存储没有标准接口。

### 2.8 Artifacts 系统

```csharp
// 步骤发布 artifact
stepRun.PublishArtifact("screenshot.png"); // 文件
stepRun.PublishArtifact(stream, "waveform.bin"); // 流

// ResultListener 处理 artifacts
class ZipArtifactsResultListener : IArtifactListener {
    void OnArtifactPublished(TestRun run, Stream artifactStream, string name) {
        // 打包所有 artifacts 为 zip
    }
}
```

链式处理：Step → Artifact1 → ArtifactListener1 → Artifact2 → ArtifactListener2 → ...

**EonTest 对比**：完全缺失。报告生成的 artifact（CSV/JSON/截图）没有统一的发布和管理管道。

### 2.9 Expressions — 表达式引擎

```
设置 "Time Delay" = "10 * 60"       → 值为 600 秒
设置 "Command" = "BANDwidth {2000*1000000/10}" → BANDwidth 200000000
设置 "结果"  = "@Scpi Step.Response" → 引用其他步骤的输出
```

支持：算术、三角函数、对数、字符串插值、环境变量、步骤输出引用、自定义函数扩展 (`IExpressionFunctionProvider`)。

**EonTest 对比**：完全不支持。Excel 驱动的工作流有一部分公式能力（通过 Excel 公式），但运行时没有表达式引擎。

### 2.10 ComponentSettings — 全局/分组/Bench 配置

```
Settings (全局)
  ├── Engine     — EngineSettings
  ├── Editor     — GUI 配置
  └── Results    — ResultSettings

Settings > Bench (带 Profile 切换)
  ├── DUTs       — DutSettings (ComponentSettingsList<IDut>)
  ├── Instruments — InstrumentSettings (ComponentSettingsList<IInstrument>)
  └── Connections — ConnectionSettings
```

**Profile 系统**：Bench 配置可以保存为 Profile，不同产线/工位切换不同 Profile。
**ComponentSettingsList** 是一个泛型集合管理器，自动处理序列化、验证、默认值。

```csharp
// 步骤中读取 Bench 配置
var dut = DutSettings.Current.FirstOrDefault(d => d.Name == "DUT1");
var inst = InstrumentSettings.GetDefaultOf<MyScope>();
```

**EonTest 对比**：有 `Recipe` 系统（SQLite 持久化参数模板），但没有等价于 `DutSettings/InstrumentSettings` 的集中式 Bench 资源管理面板。

### 2.11 Connection/Port 物理连接建模

```csharp
abstract class Connection {
    public Port Port1 { get; set; }
    public Port Port2 { get; set; }
}

class RfConnection : Connection {
    public List<LossPoint> CableLoss { get; set; } // 线缆损耗补偿
}
```

仪器/DUT 定义 Port，Connection 连接两个 Port，支持线缆损耗/方向等物理特性。

**EonTest 对比**：有 `MatrixManager` + `RouteLease` (交换矩阵抽象)，但没有 `Port` 和 `Connection` 的物理建模。这两个概念是互补的 — OpenTAP 的 Connection 是静态物理连线，EonTest 的 Matrix 是动态交换路由。

### 2.12 验证规则

```csharp
public MyTestStep() {
    // 声明式验证：构造时定义，GUI 实时检查
    Rules.Add(() => LowerLimit < UpperLimit, 
        "Lower limit must be less than upper limit", 
        nameof(LowerLimit), nameof(UpperLimit));
    
    Rules.Add(CheckConnection, "Must be connected", nameof(Instrument));
}
```

验证在 GUI 中实时生效，属性旁显示红色/黄色警告图标。

**EonTest 对比**：`IStepPlugin` 有 `preExecute()` 可以校验，但没有声明式 Rules 系统和 GUI 实时反馈。

### 2.13 嵌套 TestPlan (TestPlanReference)

```csharp
// .TapPlan XML 可直接引用另一个 .TapPlan
<TestPlanReference Path="sub_test.tapplan">
    <Parameters>
        <Parameter Name="Frequency" Value="1GHz"/>
    </Parameters>
</TestPlanReference>
```

子 TestPlan 的 External Parameters 自动提升到引用步骤上，支持参数传递和 Verdict 传播。

**EonTest 对比**：不支持。Workflow 之间完全独立，由 Orchestrator 调度。

### 2.14 Input<T> / Output 步骤间数据流

```csharp
// 步骤 A 产生输出
[Output]
public double MeasuredPower { get; private set; }

// 步骤 B 消费输入（强制要求）
[Display("Input Value")]
public Input<double> PowerInput { get; set; }

// 步骤 B.Run()
var power = PowerInput.Value; // 自动从步骤 A 获取
```

GUI 支持可视化连线：右键 Setting → Assign Output → 选择源步骤。

**EonTest 对比**：WorkflowContext.data (QVariantMap) 可以作为步骤间数据传递，但没有类型安全的 `Input<T>` 机制和 GUI 连线。

---

## 三、EonTest 独有优势（已领先）

| 能力 | 说明 |
|------|------|
| **多进程 CELL 架构** | 量产线多工位物理隔离，单 CELL 崩溃不影响其他 |
| **Lease/Heartbeat/Deadlock 资源管理** | 工业级资源租约，比 OpenTAP 简单 Open/Close 更先进 |
| **Prometheus 遥测** | Counter/Gauge/Histogram + HTTP 端点 |
| **全链路追踪** | TraceContext (TraceId/SpanId) |
| **AlertManager** | 规则评估 → 冷却去重 → 回调通知 |
| **SPC 计算** | Cp/Cpk/Pp/Ppk 统计过程控制 |
| **SchedulingPolicy** | Priority/FIFO/EDF 三种调度策略 |
| **MatrixManager** | 交换矩阵/路由抽象，batchRoute 原子路由 |
| **Canvas 编辑器** | 图形化 Workflow 编辑 |
| **Semver 兼容性** | 插件版本范围匹配 |
| **Excel 三表驱动** | .xlsx 定义 Workflow，低代码建用例 |
| **QML HMI** | 产线操作友好，支持自定义面板 |

---

## 四、EonTest 当前不足（按优先级）

### P0 — 核心差距

#### 1. DUT 未接入引擎生命周期

**现状**：IDut/Dut 接口存在，但：
- `RuntimeWorker` 中 `WorkflowContext.dut` 从未赋值
- 没有 `DutSettings` Bench 配置持久化
- 没有插件实例（Plugins/ 下无 IDut 实现）
- TestResult.dutId_ 字段未被自动填充

**对标 OpenTAP**：
```
DutSettings (Bench 配置) → PluginManager 扫描 IDut 实现
  → ResourceDependencyAnalyzer 发现步骤中的 DUT 引用
  → ResourceTaskManager 自动 Open/Close
  → [MetaData("DUT")] 自动绑定结果
```

#### 2. 缺少 ResourceOpen 属性系统

**现状**：`IResource::dependencies()` 接口已定义，但引擎未使用它来控制 Open 顺序/并行。

**要做**：实现 `ResourceOpen` 枚举（Before/InParallel/Ignore），让资源声明依赖关系时指定打开策略。

#### 3. 没有 Break/暂停 机制

**现状**：步骤执行期间无法暂停/跳过/继续。只能进程级 kill。

**对标 OpenTAP**：`BreakOffered` 事件 + `BreakCondition` + `SuggestedNextStep`。

#### 4. 结果发布管道不完整

**现状**：`TestResultCollector` 已实现但未在 RuntimeWorker 中启用。

**对标 OpenTAP**：`IResultListener` 5 回调 + `ResultTable` + `Artifacts` + `IArtifactListener`。

### P1 — 重要差距

#### 5. 缺少 Mixins 系统
运行时动态扩展步骤功能，不修改原代码。

#### 6. 缺少 Expressions 表达式引擎
数学公式、字符串插值、步骤输出引用。

#### 7. 缺少 Connection/Port 物理建模
仪器/DUT 间的物理连接建模和线缆损耗补偿。

#### 8. 没有声明式验证规则
GUI 实时显示属性验证状态。

### P2 — 增强项

#### 9. 没有 ComponentSettings 配置面板架构
Bench 资源管理、Profile 切换。

#### 10. 不支持嵌套 Workflow
Workflow 间参数传递和 Verdict 传播。

#### 11. 没有 Input<T>/Output 类型安全的数据流
步骤间类型安全的数据传递和 GUI 连线。

---

## 五、建议实施路线图（修订版）

### 第一阶段：DUT 生命周期集成（1-2 周）

```
1. 创建 Plugins/Dut/ 插件示例（如 SimpleDut）
   - 实现 IDut 接口
   - open() 通过串口/TCP 连接 DUT
   - close() 断开连接

2. 实现 DutSettings Bench 配置
   - JSON 持久化 DUT 配置列表
   - Studio 添加 DUT 配置面板

3. RuntimeWorker 中注入 DUT
   - 从 Workflow 定义中读取 dutId
   - 从 DutSettings 中查找对应 DUT 实例
   - 调用 dut->open() → 注入到 WorkflowContext.dut
   - workflow 结束后调用 dut->close()

4. TestResult 自动绑定 dutId
   - WorkflowEngine 在 runStep() 时从 context.dut 读取 dutId
   - 填充到 StepRun/TestResult
```

### 第二阶段：执行控制增强（1 周）

```
5. 实现 BreakCondition 等价物
   - 步骤定义中加 breakCondition 字段
   - WorkflowEngine 根据判决自动跳过后续步骤

6. 实现 StepRun 暂停/跳过机制
   - 对标 BreakOffered 事件
   - Studio 前端支持单步调试按钮
```

### 第三阶段：结构增强（2 周）

```
7. ResourceOpen 依赖声明
   - 资源属性加 openBehavior 元数据
   - ResourceDependencyAnalyzer 根据行为分层

8. 声明式验证规则
   - IStepPlugin 加 validate() 虚方法
   - Studio 属性面板实时显示验证状态

9. ResultTable 表格抽象
   - 对标 OpenTAP ResultTable + PublishTable
   - 支持列式大数据高效存储
```

### 第四阶段：高级特性（3-4 周）

```
10. 步骤间 Input<T>/Output 数据流
11. Mixins 动态扩展系统
12. Expressions 表达式引擎
13. Connection/Port 物理建模
14. 嵌套 Workflow 支持
```

---

## 六、附录：核心文件对照表

| 概念 | OpenTAP 文件 | EonTest 文件 | 完成度 |
|------|-------------|-------------|--------|
| 资源接口 | `Engine/IResource.cs` | `SDK/include/eon/sdk/IResource.h` | ✅ 等价 |
| 资源基类 | `Engine/Resource.cs` | — (纯接口，无基类) | ⚠️ 缺基类 |
| DUT 接口 | `Engine/IDut.cs` | `SDK/include/eon/sdk/IDut.h` | ✅ 等价 |
| DUT 基类 | `Engine/Dut.cs` | `SDK/include/eon/sdk/Dut.h` | ✅ 等价 |
| DUT 配置面板 | `Engine/DutSettings.cs` | — | ❌ 缺失 |
| 仪器基类 | `Engine/Instrument.cs` | `SDK/include/eon/sdk/ScpiInstrument.h` | ✅ 等价 |
| 仪器配置面板 | `Engine/InstrumentSettings.cs` | — | ❌ 缺失 |
| SCPI IO | `Engine/ScpiIO.cs` | `SDK/include/eon/sdk/IScpiIO.h` | ✅ 等价 |
| SCPI 仪器 | `Engine/ScpiInstrument.cs` | `SDK/src/ScpiInstrument.cpp` | ✅ 等价 |
| 步骤接口 | `Engine/ITestStep.cs` | `SDK/include/eon/sdk/IStepPlugin.h` | ✅ 等价 |
| 步骤基类 | `Engine/TestStep.cs` | — (IStepPlugin 纯接口) | ⚠️ 缺基类 |
| 步骤执行 | `Engine/TestStepRun.cs` | `SDK/include/eon/sdk/StepRun.h` | ✅ 等价 |
| Verdict | `Engine/Verdict.cs` | `SDK/include/eon/sdk/Verdict.h` | ✅ 等价 |
| 执行引擎 | `Engine/TestPlanExecution.cs` | `Runtime/src/WorkflowEngine.cpp` | ✅ 等价 |
| 资源管理 | `Engine/ResourceTaskManager.cs` | `Runtime/src/ResourceManager.cpp` | ✅ 更先进 |
| 资源依赖 | `Engine/ResourceDependencyAnalyzer.cs` | `Runtime/src/ResourceDependencyAnalyzer.cpp` | ✅ 等价 |
| 资源打开属性 | `Engine/ResourceOpenAttribute.cs` | — | ❌ 缺失 |
| 结果监听器 | `Engine/ResultListener.cs` | `SDK/include/eon/sdk/IAnalyzerPlugin.h` | ⚠️ 部分 |
| 结果表 | `Engine/ResultTable*.cs` | — | ❌ 缺失 |
| Artifacts | `Engine/IArtifactListener.cs` | — | ❌ 缺失 |
| 断点条件 | `Engine/BreakCondition.cs` | — | ❌ 缺失 |
| 全局配置 | `Engine/ComponentSettings.cs` | — | ❌ 缺失 |
| 外部参数 | `Engine/ExternalParameters.cs` | — | ❌ 缺失 |
| Mixins | `Engine/Mixins/` | — | ❌ 缺失 |
| 表达式 | 插件 (Expressions package) | — | ❌ 缺失 |
| 连接/端口 | `Engine/Port/`, `Engine/Connection.cs` | `SDK/include/eon/sdk/IMatrix.h` | 🔄 不同方案 |
