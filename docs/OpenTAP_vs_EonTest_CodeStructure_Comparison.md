# OpenTAP 9.33.0 与 EonTest 代码结构全面对比

> 生成日期: 2026-06-20
> 目的: 为 EonTest 重构提供系统性参考

---

## 目录

1. [总体架构对比](#1-总体架构对比)
2. [OpenTAP Engine/ 目录完整清单](#2-opentap-engine-目录完整清单)
3. [EonTest 目录完整清单](#3-eontest-目录完整清单)
4. [接口与类层次对比](#4-接口与类层次对比)
5. [插件系统对比](#5-插件系统对比)
6. [执行引擎对比](#6-执行引擎对比)
7. [资源管理对比](#7-资源管理对比)
8. [日志系统对比](#8-日志系统对比)
9. [结果与报表系统对比](#9-结果与报表系统对比)
10. [命名差异与结构缺口分析](#10-命名差异与结构缺口分析)
11. [重构建议](#11-重构建议)

---

## 1. 总体架构对比

### OpenTAP 分层架构

```
OpenTAP (C# / .NET)
├── Engine/               # 核心引擎层
│   ├── Core Interfaces   # ITapPlugin, IResource, IDut, IInstrument
│   ├── Test Execution    # TestPlan, TestPlanRun, TestStep, TestStepRun
│   ├── Logging           # Log, TraceSource, TraceListener, ILog, ILogListener
│   ├── Results           # ResultListener, ResultTable, ResultColumn
│   ├── Reflection        # TypeData, IMemberData, PluginTypeSelector
│   ├── Serialization     # TapSerializer, TestPlanSerializer
│   ├── Session           # TapSession
│   └── Port              # ScpiIO, Connection
├── BasicSteps/           # 内置步骤插件
│   ├── ParallelStep, SequenceStep, IfStep, RepeatStep
│   ├── SweepLoop, SweepParameterStep, LockStep
│   ├── DelayStep, ScpiStep, LoggingStep, DialogStep
│   └── TestPlanReference, ProcessStep, TimeGuardStep
├── Cli/                  # CLI 入口
├── Shared/               # 共享工具
├── sdk/                  # SDK 工具 (MSBuild, dotnet new)
├── Installer/            # 安装包
└── tests/                # 测试
```

### EonTest 分层架构

```
EonTest (C++20 / Qt6)
├── Core/                 # DDD 核心层 (Entity, EventBus, AlertManager, Metrics)
├── Domain/               # 领域层 (WorkflowDefinition, TestResult, ParameterTemplate)
├── SDK/                  # SDK 接口层 (接口定义，纯头文件 INTERFACE 库)
│   ├── IResource, IDut, IResourceManager (租约机制)
│   ├── IStepPlugin, IAnalyzerPlugin, IReporterPlugin
│   ├── IBackend, IScpiIO, ITransport, InstrumentDriver
│   └── Verdict, CapabilityRegistry, MatrixManager
├── Infrastructure/       # 基础设施层
│   ├── InstrumentManager (驱动工厂+注册表)
│   ├── HardwareManager (总线+协议栈+设备)
│   ├── Drivers/ (Multimeter, PowerSupply, SerialPort, SocketCan, TcpBus, VirtualBus)
│   ├── Backends/ (SerialBackend, PythonProcessBackend)
│   ├── Protocols/ (DoIP, UDS)
│   ├── Persistence/ (SqliteRecipeRepository, SqliteTestRepository)
│   └── ScpiIO 实现 (SerialScpiIO, TcpScpiIO, VisaScpiIO)
├── Runtime/              # 运行时引擎
│   ├── WorkflowEngine (工作流执行引擎)
│   ├── ResourceManager (资源管理器，租约机制)
│   ├── ResourceDependencyAnalyzer (依赖分析)
│   ├── CellWorker (多 CELL 支持)
│   ├── JobScheduler (P2 分布式调度)
│   └── CapabilityRegistry, MatrixManager
├── Orchestrator/         # 编排层 (SchedulingPolicy)
├── RuntimeWorker/        # 运行时代理进程入口
├── Application/          # 应用层 (RunWorkflowUseCase)
├── Studio/               # Qt Quick UI
│   ├── StudioBackend, WorkflowEditorModel
│   └── qml/ (MainWindow, pages, components)
├── Plugins/              # 插件目录
│   ├── SampleActivity, SampleAnalyzer, SampleReporter
│   ├── CsvReporter, JsonReporter, MqttReporter
│   ├── PowerSupply, Multimeter, DelayStep, ScpiStep, VoltageAnalyzer
└── ThirdParty/           # 第三方库 (miniz, QXlsx)
```

---

## 2. OpenTAP Engine/ 目录完整清单

### 2.1 核心接口 (Engine/ 根目录)

| 文件 | 类型 | 说明 |
|------|------|------|
| `ITapPlugin.cs` | 接口 | 所有插件的基接口 (空标记接口) |
| `IResource.cs` | 接口 | 资源基接口: Name, Open(), Close(), IsConnected |
| `IDut.cs` | 接口 | DUT 接口: IDut : IResource, ITapPlugin |
| `IInstrument.cs` | 接口 | 仪器接口: IInstrument : IResource, ITapPlugin |
| `IScpiInstrument.cs` | 接口 | SCPI 仪器扩展接口 |
| `IResultListener.cs` | 接口 | 结果监听器: IResultListener : IResource, ITapPlugin |
| `IResultSink.cs` | 接口 | 步骤间结果传递接口 |
| `IResultStore.cs` | 接口 | 结果存储接口 + ResultTable/ResultColumn 定义 |
| `ITestStep.cs` | 接口 | 测试步骤接口 + ITestStepParent (父子层次) |
| `ITestPlanFormat.cs` | 接口 | 测试计划格式接口 |
| `ILoopStep.cs` | 接口 | 循环步骤标记接口 |
| `IDynamicStep.cs` | 接口 | 动态步骤接口 |
| `IDynamicMemberData.cs` | 接口 | 动态成员数据 |
| `IDynamicMemberProvider.cs` | 接口 | 动态成员提供者 |
| `IExternalTestPlanParameterExport.cs` | 接口 | 外部参数导出 |
| `IExternalTestPlanParameterImport.cs` | 接口 | 外部参数导入 |
| `IMergedTableResultsListener.cs` | 接口 | 合并表结果监听 |
| `ITestPlanRunMonitor.cs` | 接口 | 测试运行监控 |
| `IValidatingObject.cs` | 接口 | 验证对象 |
| `ILockable.cs` | 接口 | 可锁定接口 |
| `IStringConvertProvider.cs` | 接口 | 字符串转换 |
| `IBreakConditionProvider.cs` | (在 TestStep.cs) | 断点条件提供 |
| `IDescriptionProvider.cs` | (在 TestStep.cs) | 描述提供 |
| `IInputOutputRelations.cs` | (在 TestStep.cs) | 输入输出关系 |
| `IParameterizedMembersCache.cs` | (在 TestStep.cs) | 参数化成员缓存 |
| `IDynamicMemberValue.cs` | (在 TestStep.cs) | 动态成员值 |
| `INotifyActivity.cs` | 接口 | 活动通知 |
| `IPictureDataProvider.cs` | 接口 | 图片数据提供 |
| `IArtifactListener.cs` | 接口 | 工件监听 |
| `IDeviceDiscovery.cs` | 接口 | 设备发现 |
| `IFormatName.cs` | 接口 | 格式名称 |
| `IInvokable.cs` | 接口 (Invokable/ 子目录) | 可调用接口 |
| `ISkippableInvokable.cs` | 接口 (Invokable/) | 可跳过调用 |
| `IVisa.cs` | 接口 | VISA 抽象 |
| `IVisaProvider.cs` | 接口 | VISA 提供者 |
| `IVisaFunctionLoader.cs` | 接口 | VISA 函数加载 |
| `ITranslationProvider.cs` | 接口 (Translation/) | 翻译提供者 |

### 2.2 核心实现类 (Engine/ 根目录)

| 文件 | 说明 |
|------|------|
| `Resource.cs` | 资源抽象基类：`abstract class Resource : ValidatingObject, IResource, INotifyActivity` |
| `Dut.cs` | DUT 基类：`abstract class Dut : Resource, IDut` (+ ID, Comment) |
| `Instrument.cs` | 仪器基类：`abstract class Instrument : Resource, IInstrument` |
| `ScpiInstrument.cs` | SCPI 仪器：`class ScpiInstrument : Instrument, IScpiInstrument` |
| `TestStep.cs` | 步骤基类：`abstract class TestStep : ValidatingObject, ITestStep, ...` |
| `TestPlan.cs` | 测试计划：`partial class TestPlan : INotifyPropertyChanged, ITestStepParent` |
| `TestPlanRun.cs` | 运行上下文：`class TestPlanRun : TestRun` |
| `TestStepRun.cs` | 步骤运行：`class TestStepRun : TestRun` |
| `TestStepList.cs` | 步骤集合 + TestStepSearch / AllowAsChildIn / AllowChildrenOfType |
| `Verdict.cs` | 判定枚举：NotSet(0) < Pass(10) < Inconclusive(20) < Fail(30) < Aborted(40) < Error(50) |
| `ResultListener.cs` | 结果监听器抽象基类 |
| `ResultSettings.cs` | 结果设置：`ComponentSettingsList<ResultSettings, IResultListener>` |
| `ResultTableOptimizer.cs` | 结果表合并优化 |
| `ResultProxy.cs` | 结果代理 |
| `ResultObjectTypes.cs` | 结果类型常量 |
| `LogResultListener.cs` | 文本日志结果监听器：`class LogResultListener : ResultListener, IFileResultStore` |
| `ConsoleTraceListener.cs` | 控制台跟踪监听器 |
| `EventTraceListener.cs` | 事件跟踪监听器 |
| `FileTraceListener.cs` | 文件跟踪监听器 |
| `Log.cs` | TraceSource 管理 + Log.CreateSource() |
| `SessionLogs.cs` | 会话日志 |
| `PluginManager.cs` | 插件管理器 (静态类，搜索+加载) |
| `PluginSearcher.cs` | 插件搜索器 |
| `PluginTypeSelector.cs` | 插件类型选择器属性 |
| `ComponentSettings.cs` | 组件设置基类 + ComponentSettingsList<TSettings, TResource> |
| `ComponentSettingsContext.cs` | 组件设置上下文 |
| `ResourceDependencyAnalyzer.cs` | 资源依赖分析 (ResourceNode) |
| `ResourceTaskManager.cs` | 资源任务管理器 |
| `EngineSettings.cs` | 引擎设置 |
| `AnnotationCache.cs` | 注解缓存 |
| `TapSerialization.cs` | TAP 序列化 |
| `UserInput.cs` | 用户输入交互 |
| `ThreadManager.cs` | 线程管理 (TapThread) |
| `WorkQueue.cs` | 工作队列 |
| `Debugger.cs` | 调试器支持 |
| `ObjectCloner.cs` | 对象克隆 |
| `ValidatingObject.cs` | 验证对象基类 |

### 2.3 Engine/ 子目录

| 子目录 | 文件 | 说明 |
|--------|------|------|
| **Annotations/** | Annotation.cs, DateTimeAnnotation.cs, IIconAnnotation.cs, 等 | 注解系统 (UI 元数据) |
| **Authentication/** | AuthenticationSettings.cs, TokenInfo.cs | 认证 |
| **Cli/** | CliActionExecutor.cs, ICliAction.cs, TestPlanRunner.cs | CLI 动作 |
| **Invokable/** | IInvokable.cs, Invokable.cs, ISkippableInvokable.cs | 可调用对象 |
| **Logging/** | diag_intf.cs (ILog, ILogContext, Event, ILogListener), diag_impl.cs (LogContext), LogFactory.cs, LogFile.cs, LogQueues.cs | 日志基础设施 |
| **Mixins/** | IMixin.cs, IMixinBuilder.cs, MixinFactory.cs, MixinTypeDataProvider.cs | Mixin 扩展 |
| **Port/** | Connection.cs, Port.cs, ConnectionSettings.cs, RfConnection.cs, SwitchPosition.cs | 端口/连接 |
| **Properties/** | AssemblyInfo.cs | 程序集信息 |
| **Reflection/** | TypeData.cs, DotNetTypeData.cs, IMemberData.cs, ITypeData.cs, 等 | 反射/类型系统 |
| **SerializerPlugins/** | ITapSerializerPlugin.cs, ObjectSerializer.cs, TestPlanSerializer.cs, TestStepSerializer.cs, 等 | 序列化插件 |
| **Session/** | TapSession.cs | 会话 |
| **TimeSpan/** | TimeSpanFormatter.cs, TimeSpanParser.cs, FormatVerbosities.cs | 时间跨度格式化 |
| **Translation/** | ITranslationProvider.cs, TranslationManager.cs, Translator.cs | 国际化 |
| **Utils/** | Utils2.cs, ExecutableFormatDetector.cs | 工具 |

---

## 3. EonTest 目录完整清单

### 3.1 Core/ 核心层

| 文件 | 说明 |
|------|------|
| `include/eon/core/Entity.h` | DDD 基类: Entity, ValueObject, DomainEvent |
| `include/eon/core/EventBus.h` | 事件总线 (Qt QObject + 发布/订阅) |
| `include/eon/core/AlertManager.h` | 告警管理器 (规则+冷却+回调) |
| `include/eon/core/Metrics.h` | 指标收集 |
| `include/eon/core/Trace.h` | 跟踪/日志 |
| `src/EventBus.cpp` | 事件总线实现 |
| `src/AlertManager.cpp` | 告警管理器实现 |

### 3.2 Domain/ 领域层

| 文件 | 说明 |
|------|------|
| `include/eon/domain/WorkflowDefinition.h` | 工作流定义: WorkflowDefinition, ActivityStep, StepExecutionPolicy, FailurePolicy |
| `include/eon/domain/WorkflowDefinitionIO.h` | 工作流定义 IO (XLSX <-> JSON) |
| `include/eon/domain/TestResult.h` | 测试结果聚合根: TestResult, StepResult |
| `include/eon/domain/IRecipeRepository.h` | 配方仓储接口 |
| `include/eon/domain/ParameterTemplate.h` | 参数模板 |
| `include/eon/domain/SpcCalculator.h` | SPC 统计计算 |
| `src/WorkflowDefinitionIO.cpp` | 工作流 IO 实现 |

### 3.3 SDK/ 接口层 (纯头文件 INTERFACE 库)

| 文件 | 说明 | 对标 OpenTAP |
|------|------|-------------|
| `IResource.h` | `class IResource` (open/close/name/isConnected/dependencies) | `IResource` |
| `IDut.h` | `class IDut : IResource` (dutId/modelName/firmwareVersion) | `IDut` |
| `IActivityPlugin.h` | IStepPlugin, IAnalyzerPlugin, IReporterPlugin + WorkflowContext | 无直接对应 |
| `IBackend.h` | `class IBackend` (串口/Python 后端抽象) | 无直接对应 |
| `IDriverPlugin.h` | BusType, BusConfig, IBusDriver, IProtocolLayer | 无直接对应 |
| `InstrumentDriver.h` | `class InstrumentDriver : QObject` (execute/query) | `Instrument` |
| `IScpiIO.h` | `class IScpiIO` (writeCommand/query/deviceClear) | `IScpiIO` |
| `ITransport.h` | `class ITransport` (原始字节读写) | `IVisa` |
| `ScpiInstrument.h` | `class ScpiInstrument : QObject, IResource, IStepPlugin` | `ScpiInstrument` |
| `ResourceManager.h` | `class ResourceManager` (租约机制: Lease) | `ResourceManager` |
| `CapabilityRegistry.h` | `class CapabilityRegistry` (插件能力注册) | 无直接对应 |
| `MatrixManager.h` | `class MatrixManager` (交换矩阵路由) | 无直接对应 |
| `Verdict.h` | `enum class Verdict` + mergeVerdicts/upgradeVerdict | `Verdict` |
| `AnalyzerResult.h` | `struct AnalyzerResult` | 无直接对应 |
| `RetryPolicy.h` | `struct RetryPolicy` (指数退避+抖动) | LockRetry |
| `TraceEvent.h` | `struct ScpiTraceEvent` (JSON Lines) | scpi.trace |
| `TelemetryExporter.h` | `class TelemetryExporter` (Prometheus) | 无直接对应 |
| `Semver.h` | `struct Semver` | `SemanticVersion` |
| `EnvSnapshot.h` | (在 Runtime/) 环境快照 | 无直接对应 |

### 3.4 Infrastructure/ 基础设施层

| 子目录 | 文件 | 说明 |
|--------|------|------|
| 根 | `InstrumentManager.cpp/h` | 仪器注册表 (单例，JSON加载，工厂) |
| 根 | `XlsxParser.cpp/h` | XLSX 解析器 |
| 根 | `HardwareManager.cpp/h` | 硬件驱动生命周期管理 |
| **Backends/** | `SerialBackend.cpp/h` | 串口后端 (QSerialPort) |
| **Backends/** | `PythonProcessBackend.cpp/h` | Python 进程后端 (QProcess) |
| **Drivers/** | `MultimeterDriver.cpp/h` | 万用表驱动 |
| **Drivers/** | `PowerSupplyDriver.cpp/h` | 电源驱动 |
| **Drivers/** | `SerialPortDriver.cpp/h` | 串口端口驱动 |
| **Drivers/** | `SocketCanDriver.cpp/h` | SocketCAN 驱动 |
| **Drivers/** | `TcpBusDriver.cpp/h` | TCP 总线驱动 |
| **Drivers/** | `VirtualBusDriver.cpp/h` | 虚拟总线驱动 |
| **Persistence/** | `SqliteRecipeRepository.cpp/h` | SQLite 配方仓储 |
| **Persistence/** | `SqliteTestRepository.cpp/h` | SQLite 测试仓储 |
| **Persistence/** | `TestResultCollector.cpp/h` | 测试结果收集器 (EventBus 监听) |
| **Protocols/** | `DoipProtocolLayer.cpp/h` | DoIP 协议层 |
| **Protocols/** | `UdsProtocolLayer.cpp/h` | UDS 协议层 |
| 根 | `ScpiInstrument.cpp/h` | SCPI 仪器实现 |
| 根 | `SerialScpiIO.cpp/h` | 串口 SCPI IO |
| 根 | `TcpScpiIO.cpp/h` | TCP SCPI IO |
| 根 | `VisaScpiIO.cpp/h` | VISA SCPI IO |

### 3.5 Runtime/ 运行时

| 文件 | 说明 | 对标 OpenTAP |
|------|------|-------------|
| `WorkflowEngine.cpp/h` | 工作流执行引擎 (PluginLoader, WorkflowContext, BreakOffered) | `TestPlan` + `TestPlanExecution` |
| `ResourceManager.cpp/h` | 资源管理器 (租约: Acquire/Release, 超时回收) | `ResourceManager` |
| `ResourceDependencyAnalyzer.cpp/h` | 资源依赖分析器 (拓扑排序, Tarjan循环检测) | `ResourceDependencyAnalyzer` |
| `CellWorker.cpp/h` | CELL 工作单元 (独立线程, 心跳, 健康监测) | 无直接对应 |
| `JobScheduler.cpp/h` | 作业调度器 (队列+分发+重试) | 无直接对应 |
| `CapabilityRegistry.cpp/h` | 插件能力注册 | 无直接对应 |
| `MatrixManager.cpp/h` | 矩阵路由管理器 | 无直接对应 |
| `EnvSnapshot.cpp/h` | 环境快照 | 无直接对应 |
| `TelemetryExporter.cpp/h` | Prometheus 遥测导出 | 无直接对应 |

### 3.6 Plugins/ 插件目录

| 插件 | 类型 | 说明 |
|------|------|------|
| `SampleActivity` | IStepPlugin | 示例活动步骤 |
| `SampleAnalyzer` | IAnalyzerPlugin | 示例分析器 |
| `SampleReporter` | IReporterPlugin | 示例报表生成器 |
| `CsvReporter` | IReporterPlugin | CSV 报表 |
| `JsonReporter` | IReporterPlugin | JSON 报表 |
| `MqttReporter` | IReporterPlugin | MQTT 报表 |
| `PowerSupply` | IStepPlugin | 电源步骤 |
| `Multimeter` | IStepPlugin | 万用表步骤 |
| `DelayStep` | IStepPlugin | 延时步骤 |
| `ScpiStep` | IStepPlugin | SCPI 命令步骤 |
| `VoltageAnalyzer` | IAnalyzerPlugin | 电压分析器 |

---

## 4. 接口与类层次对比

### 4.1 插件基接口层次

#### OpenTAP: `ITapPlugin` 层次

```
ITapPlugin (空标记接口)
│
├── IResource : INotifyPropertyChanged
│   ├── Name, Open(), Close(), IsConnected
│   │
│   ├── IDut : IResource, ITapPlugin          [Display("Dut")]
│   │   └── abstract class Dut : Resource, IDut
│   │       ├── ID (serial number)
│   │       └── Comment
│   │
│   ├── IInstrument : IResource, ITapPlugin   [Display("Instrument")]
│   │   └── abstract class Instrument : Resource, IInstrument
│   │       └── class ScpiInstrument : Instrument, IScpiInstrument
│   │           ├── visaAddress, ioTimeout
│   │           ├── ScpiQuery(), ScpiCommand()
│   │           ├── sendClearOnConnect, sendIDNOnConnect, sendCLSOnConnect
│   │           └── queryErrorAfterCommand
│   │
│   ├── IResultListener : IResource, ITapPlugin [Display("Result Listener")]
│   │   └── abstract class ResultListener : Resource, IResultListener, IEnabledResource
│   │       ├── OnTestPlanRunStart(TestPlanRun)
│   │       ├── OnTestPlanRunCompleted(TestPlanRun, Stream)
│   │       ├── OnTestStepRunStart(TestStepRun)
│   │       ├── OnTestStepRunCompleted(TestStepRun)
│   │       └── OnResultPublished(Guid, ResultTable)
│   │
│   └── IEnabledResource : IResource
│       └── IsEnabled { get; set; }
│
├── ITestStep : ITestStepParent, IValidatingObject, ITapPlugin
│   └── abstract class TestStep : ValidatingObject, ITestStep, ...
│       ├── Run(), PrePlanRun(), PostPlanRun()
│       ├── Verdict, Name, Enabled, Id
│       ├── PlanRun, StepRun, ChildTestSteps
│       └── RunChildStep(), UpgradeVerdict()
│
└── ITestStepParent
    ├── Parent, ChildTestSteps
    ├── TestPlan : ITestStepParent (根容器)
    └── TestStep (中间节点)
```

#### EonTest: SDK 接口层次

```
eon::sdk::
│
├── IResource (纯虚接口)
│   ├── virtual bool open() = 0
│   ├── virtual void close() = 0
│   ├── virtual QString name() const = 0
│   ├── virtual bool isConnected() const = 0
│   └── virtual std::vector<IResource*> dependencies() const
│   │
│   ├── IDut : IResource
│   │   ├── dutId(), modelName(), firmwareVersion(), description()
│   │   └── Qt Plugin IID: "com.eontest.sdk.IDutPlugin/1.0"
│   │
│   └── ScpiInstrument : QObject, IResource, IStepPlugin
│       ├── visaAddress, ioTimeoutMs
│       ├── sendClearOnConnect, sendIDNOnConnect, sendCLSOnConnect
│       ├── queryErrorAfterCommand
│       └── scpiQuery(), scpiCommand()
│
├── IStepPlugin
│   ├── id(), executeStep(WorkflowContext&, QString&)
│   └── Qt Plugin IID: "com.eontest.sdk.IStepPlugin/1.0"
│
├── IAnalyzerPlugin
│   ├── id(), analyze(WorkflowContext&, QVariantMap&, QString&)
│   └── Qt Plugin IID: "com.eontest.sdk.IAnalyzerPlugin/1.0"
│
├── IReporterPlugin
│   ├── id(), report(WorkflowContext&, QString&)
│   └── Qt Plugin IID: "com.eontest.sdk.IReporterPlugin/1.0"
│
├── IBackend (通信后端)
│   ├── open(config), close(), isOpen()
│   ├── write(data, timeout), readUntil(terminator, timeout)
│   └── readLine(timeout), flush(), typeName()
│
├── InstrumentDriver : QObject (仪器驱动基类)
│   ├── driverType(), open(IBackend*, config)
│   ├── execute(command, params), query(command, params)
│   └── identity(), scpiTrace()
│
├── IScpiIO (SCPI 语义层)
│   ├── writeCommand(), query(), deviceClear()
│   └── transport() -> ITransport*
│
├── ITransport (字节级传输)
│   ├── open(), close(), isConnected()
│   ├── readBytes(), writeBytes()
│   └── ... (原始字节)
│
├── IBusDriver (IDriverPlugin/)
│   ├── busType(), open(), close()
│   └── send/receive (总线级帧)
│
└── IProtocolLayer (IDriverPlugin/)
    ├── protocolType()
    └── bind(busDriver), send/receive (协议级 PDU)
```

### 4.2 执行模型层次

#### OpenTAP: TestPlan → TestStep (树形)

```
TestPlan (根容器, ITestStepParent)
├── Steps: TestStepList
│   ├── [SequenceStep] (AllowAnyChild) → 串行执行子步
│   │   ├── DelayStep
│   │   ├── ScpiStep
│   │   └── ...
│   ├── [ParallelStep] (AllowAnyChild) → 并行执行子步
│   │   ├── ...
│   │   └── ...
│   ├── [IfStep] → 条件执行
│   ├── [RepeatStep] → 循环执行
│   ├── [SweepLoop] → 扫描循环 (ILoopStep)
│   ├── [LockStep] → 互斥锁
│   └── [TestPlanReference] → 引用另一个 TestPlan
│
TestPlanRun (一次执行的上下文)
├── Verdict, Duration, Parameters
├── ResourceManager (资源生命周期)
├── TestStepRun[] (每步的运行时信息)
│   ├── Verdict, Duration, Parameters
│   └── ResultTable[] (该步发布的结果)
└── ResultListeners (接收结果回调)

执行流程:
1. TestPlan.Execute()
2. PrePlanRun() 递归 (所有 enabled 步骤)
3. Run() 递归 (步骤执行)
   ├── SequenceStep.Run() → for each child: RunChildStep()
   ├── ParallelStep.Run() → TapThread.Start() for each child
   └── Simple step → 执行自定义逻辑
4. PostPlanRun() 逆序递归
5. 资源释放
```

#### EonTest: WorkflowEngine → ActivityStep (DAG/线性)

```
WorkflowDefinition (工作流定义，从 XLSX 反序列化)
├── workflowId, entryStepId
├── initialData: QVariantMap
└── steps: QList<ActivityStep>
    ├── stepId, pluginId
    ├── parallelGroupId (并行组)
    ├── policy (maxRetries, timeoutMs, failurePolicy)
    ├── conditionKey, conditionEquals (条件执行)
    ├── compensationStepId (补偿/SAGA)
    ├── onSuccessStepId, onFailureStepId, onSkippedStepId (跳转)
    └── initialData (步骤级参数)

WorkflowContext (执行上下文)
├── workflowId, data: QVariantMap
├── resourceManager: ResourceManager*
└── dut: IDut*

执行流程 (WorkflowEngine):
1. loadPlugins(pluginDirectory) → QPluginLoader
2. capabilityRegistry_.registerFromMetadata()
3. preallocateResources() → 按拓扑序打开资源
4. 从 entryStepId 开始遍历步骤
5. 每个步骤:
   a. 检查条件 (conditionKey/conditionEquals)
   b. findStepPluginById() → IStepPlugin::executeStep()
   c. 根据返回值和 onSuccess/onFailure/onSkipped 确定下一步
   d. 支持重试 (RetryPolicy)
   e. 支持补偿 (compensationStepId)
6. releaseAllResources()
7. 合并 Verdict
```

### 4.3 判定 (Verdict) 层次

| OpenTAP `Verdict` | EonTest `Verdict` | 说明 |
|-------------------|-------------------|------|
| `NotSet = 0` | `NotSet = 0` | 未设置 |
| `Pass = 10` | `Pass = 10` | 通过 |
| `Inconclusive = 20` | `Inconclusive = 20` | 不确定 |
| `Fail = 30` | `Fail = 30` | 失败 |
| `Aborted = 40` | `Aborted = 40` | 中断 |
| `Error = 50` | `Error = 50` | 错误 |

**合并规则完全对齐**: 枚举数值越大优先级越高。

---

## 5. 插件系统对比

### OpenTAP 插件系统

```
PluginManager (静态类)
├── DirectoriesToSearch (搜索目录列表)
├── SearchAsync() → PluginSearcher
│   ├── 扫描 Assembly (CIL 元数据)
│   ├── 查找 [PluginAssembly] 标记
│   └── 缓存 Type[] 到 PluginModel
├── GetPlugins<T>() / GetPlugins(Type)
├── LocateType(name) / LocateTypeData(name)
└── AssemblyLoadFilterDelegate (加载过滤)

发现方式: 运行时反射扫描 Assembly
注册方式: [PluginAssembly(true)] 程序集属性
```

### EonTest 插件系统

```
WorkflowEngine::loadPlugins()
├── QDir(pluginDirectory).entryList("*.dll" / "*.so" / "*.dylib")
├── QPluginLoader (Qt Plugin 系统)
│   ├── loader->metaData() → JSON manifest
│   ├── CapabilityRegistry::registerFromMetadata()
│   └── qobject_cast<IStepPlugin/IAnalyzerPlugin/IReporterPlugin>
│
CapabilityRegistry
├── registerPlugin(id, version, capabilities[], resourceTypes[])
├── findPluginsByCapability(capability)
├── findPluginsByResourceType(resourceType)
└── hasCapability(pluginId, capability)

发现方式: Qt QPluginLoader + JSON Manifest
注册方式: Q_PLUGIN_METADATA(IID ... FILE "xxx.json")
```

### 差距分析

| 特性 | OpenTAP | EonTest | 问题 |
|------|---------|---------|------|
| 基接口 | `ITapPlugin` (空标记) | 无统一基接口 | EonTest 缺少 `IPlugin` 根标记接口 |
| 发现机制 | 反射扫描 Assembly | `QPluginLoader` 按 IID | 机制不同，但各自完整 |
| 能力注册 | 无显式能力注册 | `CapabilityRegistry` | EonTest 有额外能力管理 |
| Mixin 扩展 | `IMixin` 系统 | 无 | EonTest 缺少 mixin 扩展机制 |
| 序列化 | `ITapSerializerPlugin` 链 | 无序列化插件体系 | EonTest 序列化在 Domain 层硬编码 |
| 插件元数据 | 反射 Attribute | JSON `.json` manifest | EonTest 的 manifest 更轻量 |

---

## 6. 执行引擎对比

| 概念 | OpenTAP | EonTest |
|------|---------|---------|
| 执行单元 | `TestPlan` (树形, ITestStepParent) | `WorkflowDefinition` (线性/DAG) |
| 步骤 | `ITestStep` → `TestStep` (抽象基类) | `IStepPlugin` → `executeStep(context)` |
| 步骤运行 | `TestStepRun` (每个步骤独立对象) | 无独立步骤运行对象 (context.data 承载) |
| 步骤运行ID | `Guid` | 无 |
| 串行执行 | `SequenceStep` | 默认顺序遍历 |
| 并行执行 | `ParallelStep` (TapThread) | `parallelGroupId` (线程池) |
| 条件执行 | `IfStep` | `conditionKey` + `conditionEquals` |
| 循环 | `RepeatStep`, `SweepLoop`, `ILoopStep` | 无显式循环支持 |
| 循环扫描 | `SweepParameterStep`, `SweepLoopRange` | 无 |
| 重试 | LockRetry 模式 | `RetryPolicy` (指数退避) |
| 补偿/SAGA | 无 | `compensationStepId` |
| 断点 | `BreakOffered` event | `onBreakOffered` callback |
| 跳转/流程控制 | 无内置跳转 | `onSuccessStepId`, `onFailureStepId`, `onSkippedStepId` |
| 步骤引用 | `TestPlanReference` (嵌套 TestPlan) | 无 |
| 步骤锁定 | `LockStep` | 无 |
| 前置/后置 | `PrePlanRun()` / `PostPlanRun()` | 无 |
| 生命周期 | 3阶段: PrePlanRun → Run → PostPlanRun | 1阶段: executeStep() |

---

## 7. 资源管理对比

### OpenTAP 资源管理

```
ResourceTaskManager / ResourceManager
├── 管理 IResource 的 Open/Close 生命周期
├── ResourceDependencyAnalyzer
│   ├── ResourceNode (Depender, WeakDependencies, StrongDependencies)
│   └── 拓扑排序 → 确定 Open/Close 顺序
├── 资源分配: TestPlan 执行时分配
└── 资源引用: 通过属性标记 [ResourceOpen] 自动发现
```

### EonTest 资源管理

```
eon::sdk::ResourceManager
├── 租约机制 (Lease)
│   ├── LeaseMode::Shared / Exclusive
│   ├── Acquire(resourceId, mode) → Lease (RAII)
│   ├── 超时回收
│   └── 线程安全 per-entry 锁
│
├── eon::runtime::ResourceDependencyAnalyzer
│   ├── ResourceNode (strongDependencies, weakDependencies)
│   ├── 拓扑排序
│   └── Tarjan 循环依赖检测
│
├── InstrumentManager (单例，驱动注册表)
│   ├── loadFromJson() → 创建 Backend + Driver
│   ├── get(name) → InstrumentDriver*
│   └── connectAll() / disconnectAll()
│
└── HardwareManager (硬件生命周期)
    ├── loadDrivers(pluginDirectory)
    ├── openBus(config) / closeBus()
    ├── bindProtocol(busId, protocol)
    └── connectDevice(driverId, config)
```

### 差距分析

EonTest 的资源管理比 OpenTAP 更丰富:
1. **租约机制**: OpenTAP 没有显式租约，EonTest 有 `Lease` (RAII) + 共享/独占模式
2. **硬件栈**: EonTest 有 `HardwareManager` (总线→协议→设备)，OpenTAP 无此概念
3. **矩阵路由**: EonTest 有 `MatrixManager`，OpenTAP 无
4. **依赖分析**: EonTest 有 Tarjan 循环检测，比 OpenTAP 更完善

---

## 8. 日志系统对比

### OpenTAP 日志体系

```
OpenTap.Diagnostic 命名空间
│
├── ILog (接口)
│   └── LogEvent(int EventType, string Message)
│
├── ILogContext (接口)
│   ├── CreateSource(string name) → ILog
│   ├── AddListener(ILogListener)
│   └── RemoveListener(ILogListener)
│
├── LogContext : ILogContext, ILogContext2
│   ├── 后台线程处理日志队列 (LogQueue)
│   ├── LogBuffer (固定长度无锁环形缓冲区, 16K)
│   └── 分发到 ILogListener
│
├── LogFactory
│   └── CreateContext() → LogContext
│
├── ILogListener (接口)
│   └── EventsWritten(IEnumerable<Event> events)
│
├── TraceListener (抽象基类)
│   ├── TraceEvents(IEnumerable<Event> events)
│   └── TraceEvent(source, eventType, id, text)
│
├── Event (结构体)
│   ├── EventType (int), Source, Message
│   ├── Timestamp (ticks), DurationNS
│   └── ToString()
│
├── Log (静态类)
│   ├── CreateSource(string name) → TraceSource
│   ├── AddListener(ILogListener)
│   └── Flush()
│
├── TraceSource (日志源)
│   ├── log: ILog
│   ├── Owner: object
│   └── TraceEvent(type, id, message)
│
├── ConsoleTraceListener : TraceListener
│   ├── IsVerbose, IsQuiet, IsColor
│   └── ANSI 颜色支持
│
├── EventTraceListener : TraceListener
│   └── event MessageLogged
│
├── FileTraceListener : TextWriterTraceListener
│   ├── IsRelative, FileSizeLimit
│   └── FileSizeLimitReached event
│
├── SessionLogs
│
LogEventType: Error(10), Warning(20), Information(30), Debug(40)
```

### EonTest 日志体系

```
eon::core::Trace (Core/ 层)
├── 基础日志功能 (待详细分析)
│
eon::infra::TestResultCollector (通过 EventBus)
├── 监听 workflow/activity 事件
├── 构建 TestResult (聚合根)
└── 持久化到 SqliteTestRepository
│
eon::sdk::ScpiTraceEvent (SDK/ 层)
├── timestamp, cellId, stepId, dutId, resourceId
├── dir ("tx"/"rx"), payload, durationMs
├── status, error, threadId, leaseId
└── JSON Lines 格式输出
```

### 差距分析

| 特性 | OpenTAP | EonTest | 问题 |
|------|---------|---------|------|
| 日志框架 | 完整的 `ILog → LogContext → ILogListener` 管道 | 仅有 `Trace.h` 头文件 (基础) | EonTest 缺少完整日志管道 |
| 日志级别 | `Error/Warning/Information/Debug` | 待确认 | 需要对齐 |
| 日志源 | `TraceSource` (按名称隔离) | 无 | 缺少命名日志源 |
| 监听器 | `ILogListener` + `TraceListener` 层次 | 无 | 缺少可插拔日志监听器 |
| 控制台日志 | `ConsoleTraceListener` (颜色、详细) | 无 | 缺少 |
| 文件日志 | `FileTraceListener`、`LogResultListener` | 无 | 缺少文件日志 |
| 事件日志 | `EventTraceListener` | `EventBus` (不同机制) | EonTest 用事件总线而非日志管道 |
| 结构化日志 | 无 | `ScpiTraceEvent` (JSON Lines) | EonTest 在此处更先进 |
| 会话日志 | `SessionLogs` | 无 | 缺少会话日志 |
| 日志查询 | `LogFile` | `SqliteTestRepository` | 持久化机制不同 |

---

## 9. 结果与报表系统对比

### OpenTAP 结果流程

```
TestStep.Run()
  │
  ▼
stepRun.PublishResult(ResultTable)    ← 步骤发布结果
  │
  ▼
ResultSinkListener (内部)             ← IResultSink 步骤间传递
  │
  ▼
ResultListener (用户注册)              ← ResultSettings 管理
  ├── OnTestPlanRunStart/Completed
  ├── OnTestStepRunStart/Completed
  └── OnResultPublished(Guid, ResultTable)
       │
       ▼
    ResultTable
    ├── Name: string
    ├── Columns: ResultColumn[]
    │   ├── Name, ObjectType, TypeCode
    │   ├── Data: Array (typed)
    │   └── Parameters: IParameters
    └── Rows: int
```

### EonTest 结果流程

```
IStepPlugin::executeStep(context)
  │
  ▼
context.data (QVariantMap)             ← 步骤将结果写入上下文
  │
  ▼
EventBus::publish("activity/finished") ← 事件总线发布
  │
  ▼
TestResultCollector (监听 EventBus)    ← 自动收集
  │
  ▼
TestResult (聚合根)
  ├── workflowId, workflowName, batchId
  ├── cellId, dutId
  ├── startedAt, finishedAt, totalElapsedMs
  ├── overallResult (PassFail)
  └── StepResult[] 
      ├── stepId, pluginId, status
      ├── attemptCount, elapsedMs
      ├── errorMessage, outputData
      │
      ▼
  SqliteTestRepository (持久化)
```

### 差距分析

| 特性 | OpenTAP | EonTest | 问题 |
|------|---------|---------|------|
| 结果结构 | `ResultTable` + `ResultColumn` (类型化数组) | `QVariantMap` + `AnalyzerResult` | EonTest 缺少类型化结果列 |
| 结果监听 | `IResultListener` + `IResultSink` | `TestResultCollector` (EventBus) | 设计不同 |
| 发布机制 | `TestStepRun.PublishResult()` | EventBus publish | EonTest 更依赖事件驱动 |
| 报表插件 | `ILogListener` (LogResultListener) | `IReporterPlugin` (CSV/JSON/MQTT) | EonTest 有显式报表接口 |
| 分析器 | 无内置分析器概念 | `IAnalyzerPlugin` | EonTest 有显式分析器 API |
| 结果仓储 | 无内置 (由外部数据库实现) | `SqliteTestRepository` | EonTest 有内置 SQLite |
| 结果设置 | `ResultSettings : ComponentSettingsList<,>` | 无 | EonTest 缺少结果配置 UI |

---

## 10. 命名差异与结构缺口分析

### 10.1 命名不一致问题

| OpenTAP | EonTest | 问题 |
|---------|---------|------|
| `ITapPlugin` | 无 | 缺少统一插件标记接口 |
| `IResource` | `IResource` | ✅ 一致 |
| `IDut` | `IDut` | ✅ 一致 (但 EonTest 用 Qt Plugin IID) |
| `IInstrument` | `InstrumentDriver` (不同名) | ⚠️ `IInstrument` 应对标 `InstrumentDriver` |
| `ScpiInstrument` | `ScpiInstrument` | ✅ 一致 |
| `IScpiIO` | `IScpiIO` | ✅ 一致 |
| `IVisa` | `ITransport` (更通用) | ⚠️ 功能相似但命名不同 |
| `TestPlan` | `WorkflowDefinition` | ⚠️ 语义不同 (树 vs DAG) |
| `TestStep` | `IStepPlugin` | ⚠️ EonTest 无步骤基类，只有插件接口 |
| `TestPlanRun` | `WorkflowContext` | ⚠️ 语义不同 |
| `TestStepRun` | 无 | ❌ 缺少步骤运行时对象 |
| `ResultListener` | `IReporterPlugin` (部分对应) | ⚠️ 概念不同 |
| `ResultTable` | `QVariantMap` | ⚠️ EonTest 无结构化的结果表 |
| `PluginManager` | 分散在 `WorkflowEngine::loadPlugins()` | ⚠️ 无独立插件管理器 |
| `ComponentSettings` | 无 | ❌ 缺少设置持久化框架 |
| `Verdict` | `Verdict` | ✅ 完全一致 (枚举值) |
| `ResourceDependencyAnalyzer` | `ResourceDependencyAnalyzer` | ✅ 一致 |
| `RetryPolicy` | `RetryPolicy` | ✅ 一致 |

### 10.2 EonTest 特有的概念 (OpenTAP 没有)

| EonTest 概念 | 说明 |
|-------------|------|
| `WorkflowDefinition.parallelGroupId` | 并行执行组 |
| `ActivityStep.compensationStepId` | SAGA 补偿模式 |
| `ActivityStep.onSuccess/onFailure/onSkippedStepId` | 跳转/流程控制 |
| `CellWorker` | 多 CELL 独立工作线程 |
| `JobScheduler` | 分布式作业调度 |
| `HardwareManager` | 总线→协议→设备硬件栈 |
| `IBackend` | 通信后端抽象 (串口/Python) |
| `IBusDriver / IProtocolLayer` | 总线驱动 + 协议层 |
| `CapabilityRegistry` | 插件能力注册与查询 |
| `MatrixManager` | 交换矩阵路由管理 |
| `AnalyzerResult` | 分析器结果结构体 |
| `TelemetryExporter` | Prometheus 遥测导出 |
| `AlertManager` | 告警规则引擎 |
| `Entity / ValueObject / DomainEvent` | DDD 基类 |

### 10.3 EonTest 缺失的概念 (OpenTAP 有)

| OpenTAP 概念 | 缺失影响 |
|-------------|---------|
| `ITapPlugin` 统一基接口 | 插件系统缺少统一入口 |
| `TestStepRun` (Guid, Duration, Verdict, Parameters) | 无法追溯单步执行详情 |
| `PrePlanRun / PostPlanRun` 生命周期 | 无法进行批量资源预分配/清理 |
| `SequenceStep / ParallelStep` | EonTest 靠 parallelGroupId, 缺少显式的流程步骤 |
| `RepeatStep / SweepLoop / ILoopStep` | 缺少循环扫描能力 |
| `IfStep` | 条件执行能力较 OpenTAP 弱 |
| `Mixin` 系统 | 缺少动态扩展属性能力 |
| `SerializerPlugin` 链 | 缺少可扩展序列化管道 |
| `ComponentSettingsList<,>` | 缺少设置类型到资源类型的泛型容器 |
| `Annotation` 系统 | 缺少 UI 注解元数据框架 |
| `Reflection/TypeData` | 缺少运行时类型系统 |
| `Session` | 缺少用户会话管理 |
| `TestPlanReference` | 缺少嵌套工作流引用能力 |

---

## 11. 重构建议

### P0 (必须修复)

1. **添加 `IPlugin` 统一基接口**
   - 所有插件接口 (`IStepPlugin`, `IAnalyzerPlugin`, `IReporterPlugin`, `IDut`, `IBackend`) 继承自 `IPlugin`
   - 对标 OpenTAP `ITapPlugin`

2. **添加 `StepRun` 运行时对象**
   - 每个步骤执行创建 `StepRun` (含 `stepId`, `pluginId`, `startedAt`, `elapsedMs`, `verdict`, `error`, `retryCount`)
   - 关联到 `WorkflowContext`
   - 对标 OpenTAP `TestStepRun`

3. **添加结构化结果系统**
   - 定义 `ResultTable` / `ResultColumn` 类型化数据结构
   - 替代 `QVariantMap` (类型不安全)
   - 对标 OpenTAP `ResultTable`

4. **添加 `PreExecute / PostExecute` 生命周期**
   - 给 `IStepPlugin` 添加 `preExecute(context)` / `postExecute(context)` 虚方法
   - 对标 OpenTAP `PrePlanRun` / `PostPlanRun`

### P1 (推荐修复)

5. **统一日志系统**
   - 添加 `ILog` / `ILogListener` 层次
   - 添加 `TraceSource` 命名日志源
   - 对齐 OpenTAP 日志级别

6. **添加独立 `PluginManager` 类**
   - 从 `WorkflowEngine::loadPlugins()` 中解耦
   - 对标 OpenTAP `PluginManager`

7. **添加 `ComponentSettings` 框架**
   - 基于 JSON/SQLite 的设置持久化
   - 对标 OpenTAP `ComponentSettingsList<,>`

8. **添加循环/扫描支持**
   - 给 `ActivityStep` 添加 `loopCount` / `sweepRange` 字段
   - 对标 OpenTAP `ILoopStep` / `SweepLoop`

### P2 (远期规划)

9. **添加 `Mixin` 系统**
10. **添加序列化管道** (`ISerializerPlugin`)
11. **添加运行时类型系统** (`TypeData` / `IMemberData`)
12. **添加 `TestPlanReference` 嵌套引用能力**
13. **添加 UI 注解系统** (`Annotation` / `DisplayAttribute`)

---

## 附录: 关键文件对照索引

| 功能 | OpenTAP | EonTest |
|------|---------|---------|
| 插件基接口 | `Engine/ITapPlugin.cs` | 无 (建议在 `SDK/include/eon/sdk/IPlugin.h`) |
| 资源接口 | `Engine/IResource.cs` | `SDK/include/eon/sdk/IResource.h` |
| DUT 接口 | `Engine/IDut.cs` | `SDK/include/eon/sdk/IDut.h` |
| 仪器接口 | `Engine/IInstrument.cs` | `SDK/include/eon/sdk/InstrumentDriver.h` |
| SCPI 仪器 | `Engine/ScpiInstrument.cs` | `SDK/include/eon/sdk/ScpiInstrument.h` |
| SCPI IO | `Engine/Port/` | `SDK/include/eon/sdk/IScpiIO.h` |
| 传输层 | `Engine/IVisa.cs` | `SDK/include/eon/sdk/ITransport.h` |
| 步骤接口 | `Engine/ITestStep.cs` | `SDK/include/eon/sdk/IActivityPlugin.h` |
| 测试计划 | `Engine/TestPlan.cs` | `Domain/include/eon/domain/WorkflowDefinition.h` |
| 执行引擎 | `Engine/TestPlanExecution.cs` | `Runtime/include/eon/runtime/WorkflowEngine.h` |
| 执行上下文 | `Engine/TestPlanRun.cs` | 通过 `WorkflowContext` (在 IActivityPlugin.h) |
| 步骤运行 | `Engine/TestStepRun.cs` | 无 (建议添加 `StepRun`) |
| 步骤集合 | `Engine/TestStepList.cs` | 无 (内联在 WorkflowDefinition) |
| 结果监听 | `Engine/IResultListener.cs` | `SDK/include/eon/sdk/IActivityPlugin.h` (IReporterPlugin) |
| 结果表 | `Engine/IResultStore.cs` | 无 (建议添加 `ResultTable`) |
| 结果设置 | `Engine/ResultSettings.cs` | 无 |
| 判定 | `Engine/Verdict.cs` | `SDK/include/eon/sdk/Verdict.h` |
| 资源管理 | `Engine/ResourceTaskManager.cs` | `Runtime/include/eon/runtime/ResourceManager.h` |
| 资源依赖 | `Engine/ResourceDependencyAnalyzer.cs` | `Runtime/include/eon/runtime/ResourceDependencyAnalyzer.h` |
| 插件管理 | `Engine/PluginManager.cs` | 内联在 `WorkflowEngine::loadPlugins()` |
| 组件设置 | `Engine/ComponentSettings.cs` | 无 |
| 日志 | `Engine/Log.cs` + `Logging/` | `Core/include/eon/core/Trace.h` (基础) |
| 日志监听 | `Engine/Logging/diag_intf.cs` (ILogListener) | 无 |
| 控制台日志 | `Engine/ConsoleTraceListener.cs` | 无 |
| 事件日志 | `Engine/EventTraceListener.cs` | `Core/include/eon/core/EventBus.h` |
| 文件日志 | `Engine/FileTraceListener.cs`, `LogResultListener.cs` | 无 |
| 序列化 | `Engine/SerializerPlugins/` | 无 |
| 反射/类型系统 | `Engine/Reflection/` | 无 |
| 注解系统 | `Engine/Annotations/` | 无 |
| Mixin 系统 | `Engine/Mixins/` | 无 |
| 会话 | `Engine/Session/TapSession.cs` | 无 |
| 端口/连接 | `Engine/Port/` | `Infrastructure/` (ScpiIO 实现) |
| 并行步骤 | `BasicSteps/ParallelStep.cs` | `parallelGroupId` 字段 (在 WorkflowDefinition) |
| 序列步骤 | `BasicSteps/SequenceStep.cs` | 默认顺序执行 |
| 循环步骤 | `BasicSteps/LoopTestStep.cs`, `RepeatStep.cs` | 无 |
| 条件步骤 | `BasicSteps/IfStep.cs` | `conditionKey` / `conditionEquals` 字段 |
| 延时步骤 | `BasicSteps/DelayStep.cs` | `Plugins/DelayStep/` |
| SCPI 步骤 | `BasicSteps/ScpiStep.cs` | `Plugins/ScpiStep/` |
| 仪器驱动 | `BasicSteps/` 各步骤 | `Infrastructure/Drivers/` |
| CLI | `Cli/` | `Application/src/RunWorkflowUseCase.cpp` |
| GUI | (External: Editor) | `Studio/` (Qt Quick/QML) |
