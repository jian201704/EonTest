# Workflow JSON 规范

## 文件结构

```json
{
  "workflowId": "my-workflow",          // 必填，唯一标识
  "priority": 10,                        // 可选，调度优先级（默认 0，越大越优先）
  "resourceLocks": ["lock.a"],           // 可选，资源锁列表
  "entryStepId": "step.first",           // 可选，入口步骤（默认第一个 step）
  "initialData": { ... },                // 可选，全局参数（所有步骤共享）
  "steps": [ ... ]                       // 必填，步骤列表
}
```

---

## 步骤（steps[]）

每个 step 的通用字段：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `stepId` | string | 是 | - | 步骤唯一标识，供其他步骤引用 |
| `pluginId` | string | 是 | - | 执行的插件 ID |
| `maxRetries` | int | 否 | 0 | 失败重试次数 |
| `timeoutMs` | int | 否 | 0 | 超时时间 ms（0=不限）|
| `failurePolicy` | string | 否 | "fail_fast" | `"fail_fast"` 或 `"continue_on_error"` |
| `conditionKey` | string | 否 | - | 条件判断的 context 键名 |
| `conditionEquals` | string | 否 | - | 条件判断的期望值 |
| `compensationStepId` | string | 否 | - | 补偿回滚步骤 |
| `onSuccessStepId` | string | 否 | - | 成功后跳转的步骤 |
| `onFailureStepId` | string | 否 | - | 失败后跳转的步骤 |
| `initialData` | object | 否 | - | 步骤级参数，覆盖全局 `initialData` |

### 转移规则

- 步骤成功 → 跳 `onSuccessStepId`（有则跳，无则结束流程）
- 步骤失败 → 跳 `onFailureStepId`（有则跳，无则触发 fail_fast）
- 条件不满足 → 跳 `onSkippedStepId` 或 `onSuccessStepId`

---

## 插件参考

### power.supply — 可编程电源

```
pluginId: "power.supply"
```

#### 输入参数

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `port` | string | "COM3" | 串口号（兼容旧名 `powerPort`）|
| `baudRate` | int | 9600 | 波特率（兼容 `powerBaudRate`）|
| `voltage` | number | 3.3 | 目标电压 V（兼容 `powerVoltage`）|
| `current` | number | 1.0 | 限流 A（兼容 `powerCurrent`）|
| `ovp` | number | - | 过压保护 V（可选）|
| `ocp` | number | - | 过流保护 A（可选）|
| `action` | string | "on" | `"on"` 上电 / `"off"` 关电（兼容 `powerAction`）|
| `channel` | int | 1 | 输出通道（兼容 `powerOutputChan`）|
| `delay` | int | 0 | 上电后稳定等待 ms（兼容 `powerOnDelayMs`）|
| `virtualMode` | bool | false | true=跳过硬件调试 |

#### 输出到 context

| 字段 | 类型 | 说明 |
|------|------|------|
| `power.state` | string | `"on"` / `"off"` |
| `power.voltage` | number | 实测输出电压 V |
| `power.current` | number | 实测输出电流 A |
| `power.ident` | string | `*IDN?` 仪器识别 |
| `power.scpiTrace` | string | SCPI 收发记录 |

#### 示例：上电

```json
{
  "stepId": "step.power_on",
  "pluginId": "power.supply",
  "initialData": {
    "port": "COM5",
    "voltage": 5.0,
    "current": 0.5,
    "ovp": 5.5,
    "delay": 1000
  },
  "onSuccessStepId": "step.next"
}
```

#### 示例：关电

```json
{
  "stepId": "step.power_off",
  "pluginId": "power.supply",
  "initialData": {
    "action": "off"
  }
}
```

---

### measure.voltage — 数字万用表测量

```
pluginId: "measure.voltage"
```

#### 输入参数

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `dmmPort` | string | "COM4" | 串口号 |
| `dmmBaudRate` | int | 9600 | 波特率 |
| `dmmMeasureType` | string | "VOLTage:DC" | 测量类型 |
| `dmmRange` | string | "AUTO" | 量程 |
| `dmmResolution` | number | 0.001 | 分辨率 |
| `dmmSamples` | int | 1 | 采样次数取平均 |
| `virtualMode` | bool | false | true=跳过硬件（dmmPort="VIRTUAL" 同效）|

#### 输出到 context

| 字段 | 说明 |
|------|------|
| `measuredValue` | 实测值 |
| `measuredUnit` | 单位（V/A/Ohm）|
| `measuredSamples` | 有效采样数 |
| `dmmIdent` | 仪器识别 |
| `dmmScpiTrace` | SCPI 收发记录 |

#### 示例

```json
{
  "stepId": "step.measure",
  "pluginId": "measure.voltage",
  "initialData": {
    "dmmPort": "COM4",
    "dmmMeasureType": "VOLTage:DC",
    "dmmSamples": 5
  },
  "onSuccessStepId": "step.analyze"
}
```

---

### voltage.analyzer — 电压判定

```
pluginId: "voltage.analyzer"
```

#### 输入参数

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `voltageMin` | number | 3.0 | 下限 V |
| `voltageMax` | number | 3.6 | 上限 V |

读取 `power.voltage` 或 `measuredValue` 作为实测值进行判定。

#### 输出到 context

| 字段 | 说明 |
|------|------|
| `analyze.passed` | true/false |
| `analyze.value` | 实测值 |
| `analyze.min` | 下限 |
| `analyze.max` | 上限 |
| `analyze.unit` | 单位 |
| `analyze.message` | 判定描述 |

#### 示例

```json
{
  "stepId": "step.judge",
  "pluginId": "voltage.analyzer",
  "initialData": {
    "voltageMin": 3.2,
    "voltageMax": 3.4
  },
  "onSuccessStepId": "step.pass",
  "onFailureStepId": "step.fail"
}
```

---

### delay — 延时等待

```
pluginId: "delay"
```

#### 参数

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `delayMs` | int | 0 | 等待毫秒数 |

---

### sample.activity — 示例活动（可用于报告）

```
pluginId: "sample.activity"
```

无特殊参数，始终成功。常用于占位或记录。

---

## 完整示例

以下 workflow 执行：上电 → 稳定 → 测量 → 判定 → 报告 → 关电：

```json
{
  "workflowId": "measure-voltage-test",
  "priority": 10,
  "resourceLocks": ["station.power", "station.dmm"],
  "entryStepId": "step.power_on",
  "initialData": {
    "virtualMode": false,
    "voltageMin": 3.2,
    "voltageMax": 3.4,
    "port": "COM5",
    "baudRate": 9600,
    "voltage": 3.3,
    "current": 1.0,
    "dmmPort": "VIRTUAL",
    "dmmMeasureType": "VOLTage:DC",
    "dmmSamples": 3
  },
  "steps": [
    {
      "stepId": "step.power_on",
      "pluginId": "power.supply",
      "maxRetries": 1,
      "timeoutMs": 10000,
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
      "onSuccessStepId": "step.measure_voltage",
      "delayMs": 500
    },
    {
      "stepId": "step.measure_voltage",
      "pluginId": "measure.voltage",
      "maxRetries": 2,
      "timeoutMs": 10000,
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
      "pluginId": "sample.activity",
      "maxRetries": 0,
      "timeoutMs": 3000,
      "failurePolicy": "continue_on_error",
      "onSuccessStepId": "step.power_off"
    },
    {
      "stepId": "step.fail_report",
      "pluginId": "sample.activity",
      "maxRetries": 0,
      "timeoutMs": 3000,
      "failurePolicy": "continue_on_error"
    },
    {
      "stepId": "step.power_off",
      "pluginId": "power.supply",
      "maxRetries": 1,
      "timeoutMs": 1000,
      "failurePolicy": "fail_fast",
      "initialData": {
        "action": "off"
      }
    }
  ]
}
```

执行顺序：

```
power_on ──成功──→ wait_stable ──→ measure_voltage ──成功──→ judge_voltage
  │                      │                │                      │
  └──失败──→ fail_report  │          └──失败──→ fail_report      │
                          │                                      │
                                                          ┌──────┴──────┐
                                                     success          fail
                                                        │              │
                                                   pass_report    fail_report
                                                        │
                                                   power_off
```

---

## 命名约定

- **全局参数**：简短无前缀，如 `port`、`voltage`、`baudRate`
- **插件输出**：点号命名空间，如 `power.voltage`、`power.state`、`analyze.passed`
- **旧参数名仍兼容**，但建议新项目统一用新名

## 调试技巧

1. 设置 `"virtualMode": true` 可跳过硬件，纯逻辑跑通流程
2. 串口仪器设 `dmmPort: "VIRTUAL"` 单独让万用表走虚拟
3. 日志里的 `scpiTrace` 字段可看到每个 SCPI 命令和仪器响应
4. `failurePolicy: "continue_on_error"` 的步骤失败不会终止流程
