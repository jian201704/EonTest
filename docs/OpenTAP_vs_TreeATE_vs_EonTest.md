# 三大 ATE 框架对比分析：OpenTAP vs TreeATE vs EonTest

> 日期：2026-06-17

---

## 一、OpenTAP 架构精华（Keysight 开源，C#）

### 1.1 Resource 生命周期（最核心的设计模式）

```csharp
IResource { Open(); Close(); IsConnected; }
  ├─ Resource : IResource       // 基类，管理 Name + Log
  ├─ Instrument : Resource      // 仪器
  │    └─ ScpiInstrument : Instrument  // SCPI 仪器（VISA 抽象）
  ├─ Dut : Resource             // 被测件
  └─ ResultListener : Resource  // 结果监听
```

**OpenTAP 自动管理 Resource 的 Open/Close** —— 测试计划执行前自动 Open 所有 Resource，执行后自动 Close。用户不需要在测试代码里写 `serial.open()`。

```
类比 EonTest：每个 Plugin 的 executeStep 里都要自己 open/close 串口。
建议：抽离 Resource 基类，由引擎管理生命周期。
```

### 1.2 VISA 抽象层

```csharp
IScpiIO {
    DeviceClear()
    ReadSTB(ref byte stb)
    Read(byte[] buf, ref long count, ref ScpiIOResult)
    Write(byte[] buf, ref long count, ref ScpiIOResult)
    Lock(ScpiLockType)
    Unlock()
}
```

**将串口/GPIB/LAN 统一为 IScpiIO 接口**，不关心底层是 COM5 还是 TCPIP::192.168.1.30::INSTR。

### 1.3 ScpiInstrument 自动错误处理

```csharp
// ScpiInstrument 内部自动：
//   1. 发送命令前 *CLS（清除错误队列）
//   2. 发送命令后 *ESR?（读事件状态寄存器）
//   3. 自动 RaiseError(ScpiIOResult) 转异常
// 用户只需要 ScpiCommand("*IDN?") 一行
```

### 1.4 Verdict 体系

```csharp
enum Verdict {
    NotSet=0, Pass=10, Inconclusive=20, Fail=30, Aborted=40, Error=50
}
// 比 EonTest 的 PassFail (Pass/Fail/NotEvaluated) 多三个等级
```

### 1.5 TestStep 设计

```csharp
ITestStep {
    Verdict Verdict       ← 每个步骤自带判定
    bool Enabled          
    TestStepList ChildTestSteps  ← 步骤可嵌套子步骤
    TestPlanRun PlanRun    ← 运行时上下文
    void PrePlanRun()      ← 全局前置（打开资源）
    void Run()             ← 主逻辑
    void PostPlanRun()     ← 全局后置（关闭资源）
}
```

**步骤可嵌套**（子步骤组），`PrePlanRun/PostPlanRun` 只执行一次（类似 TreeATE 的 setup/teardown）。

---

## 二、三框架对比

| 架构层面 | OpenTAP | TreeATE | EonTest |
|---------|---------|---------|---------|
| **语言** | C# | C++/Qt | C++/Qt |
| **仪器抽象** | VISA(IScpiIO) 统一串口/GPIB/LAN | 无，脚本直调 | C++ DLL 插件 |
| **Resource 生命周期** | 引擎自动 Open/Close | 无 | 插件内手动 |
| **SCPI 错误自动处理** | ✅ *ESR? + 自动重试 | ❌ | ❌ |
| **Verdict 分级** | 6级(Pass/Fail/Inconclusive/Aborted/Error) | 3级(Pass/Fail/Exce) | 3级(Pass/Fail/NotSet) |
| **步骤嵌套** | ✅ ChildTestSteps | ❌ 线性 | ❌ 线性（但有 parallelGroup） |
| **Pre/PostRun** | ✅ | ✅ setup/teardown | ❌ |
| **插件发现** | 属性注解 + 自动扫描 | .tp 配置文件 | CMake 注册 |
| **结果输出** | IResultListener 多级 | IOutput 接口 | EventBus + SqliteRepository |
| **图形编辑器** | ✅ Windows/Mac GUI | ❌ | ✅ QML Studio |
| **Excel 原生** | ❌ | ❌ (CSV) | ✅ 三 Sheet |
| **多 CELL** | ❌ 单进程 | ✅ 多进程 | ✅ Orchestrator |
| **脚本语言** | C# only | JS/Python/C++ | C++ only |

---

## 三、EonTest 最值得从 OpenTAP 学习的地方

### 1. Resource 生命周期（最高优先级）⭐⭐⭐⭐⭐

```
当前问题：每个 Plugin executeStep() 都要写 serial.open()/close()
OpenTAP做法：Instrument : Resource { Open(); Close(); IsConnected; }
           → TestPlanExecution 自动调用 Open/Close

建议实现：
  class IResource { virtual bool open() = 0; virtual void close() = 0; }
  class PowerSupplyPlugin : IStepPlugin, IResource
  → WorkflowEngine 在 step.power_on 前调用 open(),
    在 workflow 结束时调用 close()
```

### 2. IScpiIO 抽象（高优先级）⭐⭐⭐⭐

```
当前问题：每个插件直接调用 QSerialPort
OpenTAP做法：IScpiIO 统一串口/GPIB/TCPIP

建议实现：
  class IScpiIO {
      virtual bool deviceClear();
      virtual QByteArray read(int timeoutMs);
      virtual bool write(const QByteArray& data);
      virtual QString errorQueue();
  }
  → SerialScpiIO, TcpScpiIO, GpibScpiIO 三个实现
  → PowerSupplyPlugin 只调 IScpiIO，不关心底层是串口还是 LAN
```

### 3. 自动 SCPI 错误处理（高优先级）⭐⭐⭐⭐

```
当前问题：发完命令不管对错
OpenTAP做法：每次查询自动 *ESR? 检查状态寄存器

建议：在 IScpiIO 层自动
  1. 发命令前 *CLS
  2. 发命令
  3. 发 *ESR?
  4. 如果有错误，自动读错误队列并抛异常
```

### 4. 步骤可嵌套（中优先级）⭐⭐⭐

```
当前问题：workflow 步骤是线性列表
OpenTAP做法：TestStep.ChildTestSteps → 步骤含子步骤

Excel 中可支持：工步号 "1.1", "1.2" 表示子步骤
```

### 5. Verdict 扩展（低优先级）⭐⭐

```
当前：PassFail (NotEvaluated/Pass/Fail)
建议：加 Inconclusive(不通过但可接受), Aborted(中断), Error(异常)
```

---

## 四、实施路线（修订后）

| 优先级 | 改进项 | 来源 | 工作量 |
|--------|--------|------|--------|
| 🔴 P0 | IResource 生命周期管理 | OpenTAP | 3天 |
| 🔴 P0 | IScpiIO 抽象层 | OpenTAP | 3天 |
| 🔴 P1 | ScpiInstrument 自动错误处理 | OpenTAP | 2天 |
| 🟡 P2 | 步骤嵌套（子步骤组） | OpenTAP | 2天 |
| 🟢 P3 | Verdict 6级方案 | OpenTAP | 1天 |
| ✅ done | Excel 三 Sheet | 自研 | - |
| ✅ done | 产线 HMI 模式 | TreeATE | 2天 |

---

## 五、结论

| 框架 | 适合 | EonTest 差距 |
|------|------|-------------|
| **OpenTAP** | 专业 RF/仪器测试，VISA 生态 | Resource 生命周期、SCPI 抽象层、自动错误处理 |
| **TreeATE** | 工厂产线端 | 结果体系、HMI 设计 — 已采纳 |
| **EonTest** | 混合（工厂+仪器）| Excel 原生 + 流程引擎 是独特优势，补充仪器抽象后可能超越两者 |
