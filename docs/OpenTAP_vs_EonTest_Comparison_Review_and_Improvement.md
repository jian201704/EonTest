# OpenTAP vs EonTest 对比文档 — 审核与改善计划（修正版）

日期：2026-06-19

---

## 一、前置说明：上一版分析存在偏袒

我上一版犯了以下错误：
1. **抠字眼反驳** — 对比稿说"扩展接口"，我理解为"说接口不存在"然后反驳"已经存在"，曲解了原意
2. **以偏概全** — 用 `ScpiInstrument` 一个类反驳"SCPI 自动化缺失"的整体判断，但 `ScpiStepPlugin` 才是产线主力，它没用 `ScpiInstrument`
3. **混淆概念** — 把事件发布等同于 PrePlanRun/PostPlanRun 生命周期钩子
4. **回避核心** — 对比稿的核心判断全部正确，我不该找借口辩解

下面诚实重评。

---

## 二、诚实评估：对比稿说的每条差距

### 2.1 资源生命周期

> OpenTAP：引擎在执行前自动 Open 所有 Resource，结束时 Close
> EonTest：多数插件在 executeStep() 内手动 open/close（已添加 IResource，但需引擎层统一管理）

**判断：正确。** 对比稿特意加了"已添加 IResource"说明它知道接口存在。核心问题是引擎不管理。验证：
- `IResource` 已定义完整接口 ✅ 但引擎不调用 ❌
- `ScpiStepPlugin` 有连接池，但仍在 `executeStep()` 内部手动管理，且不通过 IResource
- 引擎没有在 Plan 开始前统一 open、结束后统一 close

### 2.2 IO 抽象

> 已实现 IScpiIO、SerialScpiIO、TcpScpiIO、VisaScpiIO，方向一致，但需加强超时、错误检查与线程安全

**判断：正确。** 读了三份实现源码后确认：
- `VisaScpiIO::query()` 中 `viSetAttribute` 超时设置被**跳过**（注释："简化跳过"）
- `VisaScpiIO::writeCommand()` 中 `timeoutMs` 参数**未使用**（注释：`/* timeoutMs */`）
- 三种 IO 实现**都没有 mutex**，多线程不安全
- 三个 `query()` 实现中**都没有自动调用 *CLS/*ESR?/SYST:ERR?**

### 2.3 SCPI 错误自动化

> 推荐把这些操作上移到 IScpiIO 层，统一处理错误与重试策略

**判断：正确。** 验证：
- `ScpiInstrument::scpiQuery()` 做了 `deviceClear() → query() → checkScpiErrors()` — 流程正确
- 但 `ScpiStepPlugin` 不继承 `ScpiInstrument`，直接调 `io->query()`，**完全没有 SCPI 错误检查**
- 产线实际跑 ScpiStepPlugin → 自动错误处理 ≈ 没做

### 2.4 Verdict

> EonTest：Verdict 简化为 Pass/Fail/NotSet

**判断：正确。** 引擎内部有 `HardFailed/Skipped/ContinueOnError` 枚举，但这是内部运行态，不暴露到结果输出。没有 OpenTAP 的 `Inconclusive/Aborted/Error`。

### 2.5 PrePlanRun/PostPlanRun

对比稿提到 OpenTAP 支持这些钩子。详细架构报告明确指出"EonTest 缺少 Plan 级生命周期钩子"。

**判断：正确。** 我上一版用"事件发布"反驳是错误的。事件 ≠ 生命周期。引擎不会在 Plan 前统一 open 资源、结束后统一 close。

### 2.6 连接池线程安全

> 线程安全的连接池，避免频繁 open/close 带来时序问题

**判断：正确。** 当前 `s_connectionPool` = 全局 `static std::map`，无锁。多 CELL 并发一定出问题。

### 2.7 P0/P1/P2 优先级

**判断：非常合理。** 不需要调整。

---

## 三、对比稿中唯一可商榷的点

> "在接口中加入 deviceClear()、query() 超时参数与 readError()"

这三个方法**已在接口中声明**，所以"加入"这个措辞不够精确。但对比稿紧接着说：

> "在 query() 中执行 *CLS/*ESR? 验证并返回完整错误信息"

这完全正确——**实现中没有自动调用**。所以"加入"准确理解是"在实现中自动调用"而非"在接口中声明"。这是一个措辞精确性的小问题，不影响核心判断。

除此之外，对比稿没有其他明显问题。

---

## 四、我上一版犯了什么错

| 我上一版说 | 实际是错的，因为 |
|-----------|----------------|
| "IScpiIO 已定义 deviceClear/readError = 事实错误" | 对比稿说的是"实现中自动调用"，不是"接口声明"。我曲解了 |
| "ScpiInstrument 有 checkScpiErrors = 明显低估" | ScpiStepPlugin 不用它，产线实际跑=没有 |
| "WorkflowEngine 已有事件 = 明显低估" | 事件 ≠ 生命周期钩子。我混淆了概念 |
| "补偿步骤 = 嵌套步骤替代方案" | 补偿和嵌套是不同概念，我强行辩解 |
| 评分 70/100 | 对比稿应得 95+ 分 |

**根因：** 我关注"接口有没有声明"，对比稿关注"引擎用不用、产线用不用、线程安全不安全"。我在回答错误的问题。

---

## 五、改善计划

### 5.1 对比稿本身

对比稿核心内容不需要修改。唯一可补充：
- 在 IScpiIO 说明处加注：接口已声明 `deviceClear()/readError()/query(timeoutMs)`，但**实现中未自动调用**
- 提及 `ScpiInstrument` 基类已封装自动错误检查，但 `ScpiStepPlugin` 未使用

### 5.2 EonTest 实际改善（按对比稿路线执行）

直接按对比稿的 P0/P1/P2 路线执行，不需要调整优先级。

**P0 立即做：**
1. 实现 `ResourceManager`：引擎在 Plan 开始前扫描所有 resource 调用 `open()`，结束后 `close()`
2. 让 `ScpiStepPlugin` 自动执行 `*CLS → command → *ESR? → SYST:ERR?`
3. 给连接池 `s_connectionPool` 加 `std::mutex`
4. 补上 `VisaScpiIO::query()` 中跳过的 `viSetAttribute` 超时
5. 用上 `VisaScpiIO::writeCommand()` 中未使用的 `timeoutMs`

**P1 跟进：**
1. 连接池升级为引用计数 + 独占/共享模式
2. Verdict 扩展到 5 级并输出到结果
3. scpi.trace 改为 JSON Lines 结构化

**P2 长远：**
1. PrePlanRun/PostPlanRun 钩子
2. 步骤嵌套 + 子步骤组
3. OpenTAP 兼容导出器

---

## 六、结论

**对比稿评分：95/100**

对比稿的核心判断全部正确，工程路线清晰，优先级合理。唯一可吹毛求疵的是 IScpiIO 接口方法的措辞精确性，不影响实质价值。

**自我批评：** 我上一版过于关注"接口存在不存在"这个表层问题，忽略了"引擎用不用"、"产线用不用"、"线程安全不安全"这些实质差距。对比稿关注的是后者，我却在纠结前者。这是避重就轻，道歉。
