# EonStudio 使用分析与修复方案

> 生成日期：2026-06-15

---

## 一、EonStudio 当前怎么用

### 整体架构回顾

```
┌─────────────────────────────────┐
│  EonStudio (Qt6/QML GUI)        │
│  ├─ Dashboard 仪表盘            │
│  │   ├─ 任务列表                │
│  │   └─ 实时日志                │
│  └─ Workflow Editor 图形编辑器  │
│      ├─ 画布节点拖拽            │
│      └─ 属性面板                │
└──────────┬──────────────────────┘
           │ 启动子进程 (QProcess)
           ▼
┌─────────────────────────────────┐
│  Orchestrator (调度进程)        │
│  ├─ 任务队列                    │
│  ├─ 资源锁                      │
│  ├─ SQLite 状态持久化           │
│  └─ 多 CELL 分发                │
└──────────┬──────────────────────┘
           │ spawn N 个 Worker
           ▼
┌─────────────────────────────────┐
│  RuntimeWorker × N CELL         │
│  ├─ WorkflowEngine 执行图       │
│  ├─ 插件加载 (IStepPlugin等)    │
│  └─ 遥测事件上报 (stdout JSON)  │
└─────────────────────────────────┘
```

### 当前能用的功能

1. **Dashboard 页面**：配置插件目录、State DB 路径、CELL 数量，点击 **▶ Run** 执行选中 workflow，右侧显示遥测卡片（Passed/Failed/Pass Rate 等）、任务表格和实时日志。

2. **Workflow Editor 页面**：可视化拖拽节点、设置连线（success/failure/skipped/compensation）、编辑节点属性（重试次数、超时、失败策略等）、导出/导入 JSON。

3. **CLI 命令行方式**（更成熟）：

```bash
# 单 workflow
./bin/eon-orchestrator.exe ./build/Plugins ./Workflows/minimal.workflow.json

# 多 CELL + 多 workflow
./bin/eon-orchestrator.exe --cells 2 ./build/Plugins ./Workflows/minimal.workflow.json ./Workflows/parallel.workflow.json

# 带状态持久化和重试策略
./bin/eon-orchestrator.exe --cells 2 --state ./state.db --max-task-retries 2 --retry-backoff-ms 300 ./build/Plugins ./Workflows/minimal.workflow.json

# 从状态恢复
./bin/eon-orchestrator.exe --resume --state ./state.db
```

---

## 二、测电压场景：当前缺失的关键部分

### 需求分解

> 控制电源上电 → 控制万用表读电压 → 判断结果 → 显示

### 2.1 现有插件接口

| 接口 | 文件 | 职责 |
|------|------|------|
| `IStepPlugin` | `SDK/include/eon/sdk/IActivityPlugin.h` | 执行测试步骤 |
| `IAnalyzerPlugin` | 同上 | 数据分析/判定 |
| `IReporterPlugin` | 同上 | 结果上报（CSV/JSON/MQTT） |
| `IDriverPlugin` | `SDK/include/eon/sdk/IDriverPlugin.h` | 硬件驱动（Serial/CAN/I2C/SPI/GPIB 等） |

### 2.2 实际实现了什么？

在 `Studio/src/main.cpp` 里，可用插件列表硬编码了这些名字：

```cpp
editorModel->setAvailablePlugins({
    "sample.activity", "sample.analyzer", "sample.reporter",
    "can.send", "can.receive", "uds.readDID", "uds.writeDID",
    "serial.send", "gpio.set", "delay", "measure.voltage"
});
```

但实际存在的插件 **只有 3 个 Sample**：
- `Plugins/SampleActivity/` — 假的，只返回 `sampleResult = "ok"`
- `Plugins/SampleAnalyzer/` — 同上
- `Plugins/SampleReporter/` — 同上

**`measure.voltage`、`serial.send`、`gpio.set`、`can.send` 等都只是名字，没有对应的插件 DLL 实现！**

### 2.3 测电压需要的完整 Workflow

```json
{
  "workflowId": "measure-voltage-test",
  "priority": 10,
  "resourceLocks": ["station.power", "station.dmm"],
  "entryStepId": "step.power_on",
  "initialData": {
    "targetVoltageMin": 3.2,
    "targetVoltageMax": 3.4,
    "dmmAddress": "USB0::0x1AB1::0x0588::DM3R123456::INSTR"
  },
  "steps": [
    {
      "stepId": "step.power_on",
      "pluginId": "power.supply",
      "maxRetries": 1,
      "timeoutMs": 5000,
      "failurePolicy": "fail_fast",
      "onSuccessStepId": "step.wait_stable",
      "onFailureStepId": "step.fail_report"
    },
    {
      "stepId": "step.wait_stable",
      "pluginId": "delay",
      "maxRetries": 0,
      "timeoutMs": 0,
      "failurePolicy": "continue_on_error",
      "onSuccessStepId": "step.measure_voltage"
    },
    {
      "stepId": "step.measure_voltage",
      "pluginId": "measure.voltage",
      "maxRetries": 2,
      "timeoutMs": 3000,
      "failurePolicy": "fail_fast",
      "onSuccessStepId": "step.judge_voltage",
      "onFailureStepId": "step.fail_report"
    },
    {
      "stepId": "step.judge_voltage",
      "pluginId": "voltage.analyzer",
      "maxRetries": 0,
      "timeoutMs": 0,
      "failurePolicy": "continue_on_error",
      "onSuccessStepId": "step.pass_report",
      "onFailureStepId": "step.fail_report"
    },
    {
      "stepId": "step.pass_report",
      "pluginId": "csv.reporter",
      "maxRetries": 0,
      "timeoutMs": 0,
      "failurePolicy": "continue_on_error"
    },
    {
      "stepId": "step.fail_report",
      "pluginId": "csv.reporter",
      "maxRetries": 0,
      "timeoutMs": 0,
      "failurePolicy": "continue_on_error"
    }
  ]
}
```

### 2.4 需要新开发的插件

| 插件名 | 类型 | 功能 |
|--------|------|------|
| `power.supply` | IStepPlugin | 通过串口/GPIB 发 SCPI 命令控制电源 |
| `measure.voltage` | IStepPlugin | 通过串口/GPIB 控制万用表读取电压 |
| `delay` | IStepPlugin | 等待指定毫秒数 |
| `voltage.analyzer` | IAnalyzerPlugin | 判断电压是否在上下限范围内 |
| `csv.reporter` | IReporterPlugin | 已有目录，需确认实现 |

---

## 三、多 CELL Layout 问题分析与修复方案

### 3.1 问题诊断

当前 `MainWindow.qml` 的 Dashboard 布局是**完全静态**的：

```qml
// 8 个仪表盘卡片是硬编码的，不随 CELL 数量变化
RowLayout {
    DashboardCard { title: "Workflows"; ... }
    DashboardCard { title: "Passed"; ... }
    DashboardCard { title: "Failed"; ... }
    DashboardCard { title: "Pass Rate"; ... }
    DashboardCard { title: "Steps Fin"; ... }
    DashboardCard { title: "Steps Fail"; ... }
    DashboardCard { title: "Retries"; ... }
    DashboardCard { title: "Tasks"; ... }
}

// 任务列表只有一个 ListView，不区分 CELL
ListView { id: taskView; model: backend.taskListModel ... }
```

`StudioBackend.h` 中的 `TaskInfo` 也没有 `cellId` 字段：

```cpp
struct TaskInfo {
    int taskId = 0;
    QString workflowId;
    QString workflowPath;
    QString status = "pending";
    int attempt = 0;
    int maxAttempts = 0;
    int priority = 0;
    int exitCode = 0;
    QString lastError;
    // 缺少: int cellId = -1;
};
```

### 3.2 修复方案

#### 修改 1：`TaskInfo` 增加 `cellId` 字段
- 文件：`Studio/include/eon/studio/StudioBackend.h`
- 新增 `int cellId = -1;`
- `TaskListModel` 增加 `CellIdRole`

#### 修改 2：`MainWindow.qml` Dashboard 改为动态布局
- 当 `cellCount <= 1` 时，显示原来的全局总览视图（不变）
- 当 `cellCount >= 2` 时，改为按 CELL 分组的 Grid 布局：
  - 顶部保留全局聚合卡片
  - 下方用 GridView/Repeater 按 CELL 数量动态生成每 CELL 的任务列
  - 每列显示该 CELL 的任务列表、状态等

#### 修改 3：`StudioBackend` 解析遥测 JSON 时提取 cellId
- 在 `onProcessOutput()` 中解析 JSON 行时提取 `cellId` 字段
- 更新 `TaskInfo` 时填入对应的 `cellId`

---

## 四、总结

| 你想做的事 | 当前状态 | 需要做什么 |
|-----------|---------|-----------|
| 在 Studio 里跑通 workflow | ✅ 可以 | 用 sample.activity 跑示例 workflow |
| 控制硬件（电源/万用表） | ❌ 不行 | 需要开发 Driver 插件 + Step 插件 |
| 多 CELL 分栏显示 | ❌ 不行 | 需要改 QML Layout + TaskListModel + 遥测解析 |
| 结果判定与显示 | ⚠️ 部分 | Analyzer 接口有但无真实实现 |

### 建议的开发优先级

1. **修复多 CELL Layout**（本文档第三节方案）
2. **实现硬件驱动层**：`PowerSupplyDriver` + `MultimeterDriver`（串口 SCPI）
3. **实现 Analyzer 插件**：电压上下限判断
4. **完善 Reporter**：确认 `CsvReporter`/`JsonReporter`/`MqttReporter` 实现
