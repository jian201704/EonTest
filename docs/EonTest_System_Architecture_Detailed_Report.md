EonTest — 系统级架构详解与实施计划
=====================================

生成时间: 2026-06-19

目的
-----
将此前三次讨论（OpenTAP 对比、系统级建议、P0–P2 路线）合并成一份技术细化的实施级文档，聚焦资源管理、并行（多 CELL）、SCPI/IO 行为、日志/报告、插件能力与分布式扩展。文档同时给出接口原型、并发语义、读写回路示例、日志 schema、聚合器行为、测试策略与可交付时间估算，便于工程实现。

**执行摘要**
- 建议 P0：实现 `ResourceManager`（租赁/独占/引用计数/超时回收）、把现有连接池改为可租赁模式、引入 per-cell worker（隔离日志与故障）、统一 `IScpiIO` 的超时与错误回读策略、以及结构化 `scpi.trace`（JSON Lines）与聚合器。
- P1/P2：矩阵路由抽象、插件 manifest 与能力发现、Studio 并行看板、远程 Agent/Runner（分布式扩展）。

目录
-----
1. 资源管理（ResourceManager）详细设计
2. 连接池与租赁（Scpi/ VISA 等）
3. `IScpiIO` 与 SCPI I/O 边界条件与实现细节
4. `VisaScpiIO` 的改进要点
5. `scpi.trace` JSON Schema 与聚合器实现
6. per-cell Worker 设计（进程 VS 线程、通信、心跳）
7. 矩阵/路由抽象接口建议
8. 调度器与预分配策略（多 CELL 并行）
9. 插件 manifest 与 capability discovery
10. 测试计划、验收标准与 CI 要求
11. P0–P2 分阶段实施计划与时间估算
12. 下一步（开发任务与命令）

1) 资源管理（ResourceManager）详细设计
-----

目标: 在引擎层统一管理设备/资源的打开、租赁（Lease）、并发访问与回收，避免插件各自 open/close 导致的竞态和泄露。

核心抽象
- `ResourceManager`：负责登记资源（配置信息）、维护资源条目表、处理 `Acquire` / `Release` 请求、审计租赁历史、超时回收。
- `Lease`（资源租约）：RAII 风格，析构时自动释放。包含对 `IResource` 或 `IScpiIO` 的引用和租赁模式信息。

接口原型（头文件概念示例）:

```cpp
// SDK/include/eon/sdk/ResourceManager.h
#include <chrono>
#include <memory>
#include <string>

enum class LeaseMode { Shared, Exclusive };

struct IResource; // forward
struct IScpiIO; // forward

class ResourceManager {
public:
  struct Lease {
    std::string resourceId;
    LeaseMode mode;
    std::shared_ptr<IResource> resource; // non-owning pointer ok if manager keeps ownership
    std::shared_ptr<IScpiIO> scpi; // optional
    void Release();
    ~Lease();
  };

  // 尝试获取 lease，超时后返回 nullptr
  std::unique_ptr<Lease> Acquire(const std::string &resourceId, LeaseMode mode,
                                 std::chrono::milliseconds timeout);

  // 预分配（在调度前）
  bool Preallocate(const std::string &resourceId, LeaseMode mode);
};
```

内部数据结构
- 主表: unordered_map<string, ResourceEntry>
- ResourceEntry:
  - pointer to IResource (or factory to create)
  - state (Closed/Opening/Open/Closing)
  - int sharedCount
  - bool hasExclusiveOwner
  - queue of waiting requests (Store requester id + mode + condition variable)
  - mutex per entry

Acquire 算法要点
- 使用 per-entry mutex 保护 state 与 counts，避免全局锁成为瓶颈。
- Exclusive 请求：等待直到 sharedCount == 0 && !hasExclusiveOwner。
- Shared 请求：可以并行，只要没有排队的 Exclusive 请求（可选公平策略）。
- 当底层 IResource 未打开时，负责打开（同步或异步）并标记 state。首次打开失败要向等待者播报错误。
- Lease 在 Release 时减少计数，若计数变 0 且 policy 是即时关闭，则关闭底层资源或把它放入“warm pool”。

超时与回收
- Acquire 带超时，超时返回 nullptr 或抛异常。
- 强制回收: 如果 Lease 超过最大生存期，ResourceManager 可记录并尝试调用资源的 reset/close 并释放登记，发出告警。

审计与可观察性
- 为每次 Acquire/Release 写入 audit 日志（资源 id、leaseId、caller stepId、timestamp、duration、outcome）。便于事后追踪资源争用原因。

失败场景与防护
- 若 open() 失败，等待队列中所有请求应收到明确错误并清理等待状态。

执行模型与生命周期钩子（PrePlanRun / PostPlanRun / 子步骤支持）
-----

背景与目标
- OpenTAP 支持在 Plan 级别的一次性资源准备（PrePlanRun）与收尾（PostPlanRun），并支持嵌套步骤（子步骤）与各种级别的生命周期钩子。EonTest 当前以线性执行并在需要时支持并发组，但缺少明确的 Plan 级别生命周期钩子与子步骤资源作用域声明。目标是补充这些钩子以：
  - 提前在 Plan 启动时预分配/打开全局资源，减少运行中重复开/关；
  - 在 Plan 结束时统一清理资源并生成最终报告；
  - 支持嵌套步骤的资源继承与隔离策略，便于表达复杂测试序列与子流程复用；
  - 与 `ResourceManager` 集成，明确资源 scope（`plan` / `step` / `substep` / `cell`）。

设计要点
- 生命周期钩子（建议引擎 API）:

```cpp
// WorkflowEngine 概念接口
class WorkflowEngine {
public:
  bool PrePlanRun(const Plan &plan, PlanContext &ctx);   // 在任何 step 之前调用
  void PostPlanRun(PlanContext &ctx);                    // 在所有 step 完毕后调用

  bool PreStepRun(const Step &step, StepContext &ctx);  // 每个 step 进入前
  void PostStepRun(StepContext &ctx);                    // 每个 step 退出后

  bool PreSubstepRun(const Substep &sub, SubstepContext &ctx); // 可选，支持嵌套子步骤
  void PostSubstepRun(SubstepContext &ctx);
};
```

- 资源作用域（Resource Scope）: 在 workflow/step 定义中为资源声明 `scope` 字段：`plan`（在 PrePlanRun 申请并在 PostPlanRun 释放）、`step`（在 PreStepRun 申请并在 PostStepRun 释放）、`substep`（更细粒度）或 `cell`（按 cell 实例化）。

- PrePlanRun 语义:
  - 引擎在执行任何 steps 之前扫描 plan，汇总所有 scope=`plan` 的资源需求；
  - 调用 `ResourceManager::Preallocate(resourceId, count)` 或 `Acquire` 并把 Lease 存入 `PlanContext`；
  - 若 Preallocate 失败（资源不足或冲突），Plan 可立即失败或根据策略（等待/退让/降级）处理；
  - PrePlanRun 也可启动共享服务（如远端 Agent、数据库连接、遥测采集器）。

- PostPlanRun 语义:
  - 引擎确保释放 PlanContext 中的所有 lease（调用 `Release()`），执行统一关闭/flush 动作并触发最终报告/聚合；
  - 即使个别 step 出错，PostPlanRun 仍应保证尽最大努力清理（带重试/backoff）。

- 嵌套步骤（子步骤）语义:
  - 子步骤默认继承父步骤的上下文（包括可见的 plan-level leases）；
  - 子步骤可声明自己的资源需求（scope=`substep` 或 `step`），由 PreSubstepRun/PreStepRun 分别处理；
  - 当父步骤为并发组时，子步骤的 `exclusive` 请求必须在同一 worker/agent 上生效，或通过 ResourceManager 做跨 worker 的协调。

实现示例（伪代码）:

```cpp
bool WorkflowEngine::RunPlan(const Plan &plan) {
  PlanContext ctx;
  if (!PrePlanRun(plan, ctx)) return false;

  for (auto &step : plan.steps) {
    StepContext sctx{&ctx};
    if (!PreStepRun(step, sctx)) { /* consider abort or mark failed */ }
    // 支持 nested: step may contain substeps or a parallel group
    ExecuteStep(step, sctx);
    PostStepRun(sctx);
  }

  PostPlanRun(ctx);
  return true;
}
```

配置示例（workflow JSON 片段）:

```json
{
  "planId": "example",
  "resources": [
    { "id": "VISA::GPIB::1", "scope": "plan", "mode": "exclusive" },
    { "id": "MATRIX::A", "scope": "step", "mode": "exclusive" }
  ],
  "steps": [
    {
      "id": "step1",
      "type": "ScpiStep",
      "resources": [ { "id": "MATRIX::A", "scope": "step" } ],
      "substeps": [ /* 可嵌套 */ ]
    }
  ]
}
```

并发组与 PrePlanRun 的交互
- 对于多 CELL 并行执行，PrePlanRun 可按 cell 数量为某些资源做批量预分配（例如为 N 个 cell 分配 N 个测量通道）；`ResourceManager::Preallocate(resourceId, count)` 返回一组 Lease 或失败。
- 若资源为单实例（如共享电源），但在 Plan 中以 `scope=plan` 声明，则该资源在整个 Plan 中处于单一租约，多个 cell 必须通过内部路由（MatrixManager）或排队共享访问。

错误处理建议
- PrePlanRun 失败应尽早暴露并终止 Plan（除非策略允许降级）；
- 如果某 step 在运行中要求了新的 plan-level 资源，应拒绝或改为 step-level 临时申请并记录警告（强烈建议在调度前校验）。

与 ResourceManager 的集成点
- PrePlanRun 调用 `ResourceManager::Preallocate` 并把返回的 Lease 存入 `PlanContext`；
- WorkflowEngine 在创建 CellWorker 时把 PlanContext（或对 plan-level leases 的引用）一并传递，确保 worker 能直接使用已打开的 session/连接；
- PostPlanRun 负责统一 Release 与强制回收。

可观测性
- 在 PrePlanRun 与 PostPlanRun 中分别记录 audit 事件（`preplan.acquire` / `postplan.release`），并把 leaseId 与计划 runId 关联到 `scpi.trace` 事件，便于后续追踪。

兼容性与迁移策略
- 通过在步骤 manifest 中增加 `scope` 字段逐步生效；对旧步骤（未指定 scope）默认按 `step` 行为处理。

性能注意
- PrePlanRun 在大型 Plans 中会增加启动延迟（资源预配置成本），建议支持异步预热（并行开通多个设备）与进度报告。

2) 连接池与租赁（Scpi/Visa）
-----

改造思路
- 停用插件内部的全局静态池直接访问，改为通过 `ResourceManager::Acquire(resourceId, mode)` 获取 `Lease`，从 `Lease` 获取 `IScpiIO` 对象。
- 连接池内部仍可保留“已打开的 session/cookie”，但其借出语义应由 ResourceManager 管理。

共享 vs 独占策略
- 多数串口与 GPIB 设备不允许并发命令 → 默认设为 `Exclusive`。
- 某些设备（可并行采样的虚拟设备或多个传感通道）可暴露为 `Shared`。

并发控制实现
- 在 `IScpiIO` 对象内部提供一个 `std::mutex ioMutex`，以序列化单个会话的写/读操作（适用于 shared 模式下的访问）；在 Exclusive 模式，可跳过该锁以减少开销。

示例：`ScpiStepPlugin` 修改要点
- 旧流程: 直接从静态池取 `IScpiIO*` 并调用 `query()`。
- 新流程: `auto lease = ResourceManager::Acquire(res, Exclusive, 5s); if(!lease) fail; auto scpi = lease->scpi; scpi->query(...);`。

3) `IScpiIO` 与 SCPI I/O 细节
-----

通用语义
- `open() / close()`：打开/关闭底层 transport（串口、socket、visa session）。
- `write(cmd)`：发送命令（不等待响应）。
- `query(cmd, timeout)`：写命令后等待响应并返回（trim 与处理 echo）。

稳定的 read-loop（示例伪代码）:

```cpp
// 超时以毫秒为单位
std::string query(IScpiIO &io, const std::string &cmd, int timeoutMs) {
  io.write(cmd + io.getTerminator());
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  std::string buf;
  while (std::chrono::steady_clock::now() < deadline) {
    if (io.hasBytesAvailable()) {
      buf += io.readAvailable();
      if (buf.find(io.getTerminator()) != std::string::npos) break;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  // 处理 echo、裁剪 terminator、trim whitespace
  return normalizeResponse(buf);
}
```

注意事项
- Terminator 可配置（"\n"、"\r\n"）；对某些设备要处理 echo（设备会回显命令），对某些设备命令后没有 terminator（使用长度或定时结束）。
- 对 `query()` 的实现要尽量避免 busy-loop，使用带超时等待的事件或 `select`/`waitForReadyRead`。
- `query()` 成功后建议自动做一次 `SYST:ERR?`（可配置）以捕获隐藏的内部错误。若 `SYST:ERR?` 报告非零错误，记录并根据策略重试/失败。

4) `VisaScpiIO` 的改进要点
-----

要点
- 动态加载 VISA 实现（NI-VISA / Keysight VISA）并映射状态码。
- 使用 VISA 属性设置超时（`VI_ATTR_TMO_VALUE`，单位 ms）：

```cpp
// pseudo
viSetAttribute(session, VI_ATTR_TMO_VALUE, (ViAttr)timeoutMs);
```

- 读取: 用 `viRead` 循环读取到 terminator 或 byte count。注意 `viRead` 的返回码 (VI_SUCCESS, VI_SUCCESS_MAX_CNT)，并在错误时解释并上报。

错误处理
- 在 VISA 层将底层错误码映射到统一的 `IScpiIO::Error` 枚举并记录详细信息（status description + errno）。

5) `scpi.trace` JSON Schema 与聚合器
-----

建议格式（JSON Lines，每行为单个事件）:

```json
{
  "ts": "2026-06-19T12:34:56.789Z",
  "cellId": "cell-01",
  "stepId": "workflow-XYZ.step-12",
  "resourceId": "VISA::TCPIP::10.0.0.1::5025::SOCKET",
  "dir": "tx",
  "payload": "*IDN?",
  "duration_ms": 12,
  "status": "ok",
  "error": null,
  "threadId": 12345,
  "leaseId": "uuid-v4"
}
```

字段说明
- `ts`: ISO8601 UTC 时间戳
- `cellId`/`stepId`: 便于聚合与回溯
- `dir`: `tx` 或 `rx`
- `payload`: 可读字符串（必要时使用 base64 存储二进制）
- `duration_ms`: 从发送到收到响应的时间
- `status`/`error`: `timeout`/`error`/`ok`

聚合器行为
- 实现一个独立工具（Python/C++）读取 `reports/cell-*/scpi.trace.jsonl`，按 `ts` 合并并输出：
  - `reports/summary-<run>.json`（按 cell 聚合的统计）
  - `reports/combined-<run>.jsonl`（整机按时间排序的事件流）

6) per-cell Worker 设计
-----

两种实现路径
- 线程内 Worker（轻量）: 低延迟、共享进程内资源，但崩溃会带来全局影响。
- 进程/Agent Worker（推荐）: 每个 CELL 运行独立进程或容器，通过 RPC/IPC（gRPC/ZeroMQ/Unix domain sockets/Windows named pipes）与 `WorkflowEngine` 通信。更安全、易于重启与隔离。

通信模型（进程模式）
- `Engine` <-> `CellAgent` 使用 JSON-RPC over TCP 或 gRPC。
- `Engine` 下发任务（step + parameters + leaseId if pre-acquired），`CellAgent` 返回结果与事件流（写入本地 `reports/cell-<id>/`）。

心跳与 Watchdog
- `CellAgent` 每 2s 发送心跳，Engine 在 3 个心跳间隔无应答则认定为 down，转入重试或重分配模式。

日志与存储
- 每个 agent 将日志写入 `reports/cell-<id>/`：`scpi.trace.jsonl`、`step-results.jsonl`、`agent.log`。

7) 矩阵/路由抽象
-----

接口示例

```cpp
class MatrixManager {
public:
  // 申请路由并返回一个 RouteLease：析构时自动断开路由
  std::unique_ptr<RouteLease> RouteTo(const std::string &src, const std::string &dst,
                                      std::chrono::milliseconds timeout);
};
```

实现要点
- 路由请求通过 `ResourceManager` 获取对矩阵硬件的独占控制，执行 connect/verify，返回 RouteLease。
- 支持批量路由事务：多个连接在单个租约内设置并一并回滚。

8) 调度器与预分配策略（多 CELL 并行）
-----

策略要点
- 提交 Job（N cells）时，调度器先行 `Preallocate` 所需资源（pdus, meters, matrix）。若所有资源可预分配则同时开始；否则按优先级/等待策略重排。
- 预分配模式应区分“必须独占”与“可共享”。可共享资源用较松策略以提高吞吐。

简单调度伪算法

1. 解析 Job 所需资源集合 R
2. 对 R 中每一项调用 `ResourceManager::Preallocate`（短超时）
3. 若全部成功，分配给 N 个 CellAgent 并启动
4. 若部分失败，依据策略（等待/退让/重试）处理

9) 插件 manifest 与 capability discovery
-----

示例 manifest（plugin.json）

```json
{
  "name": "ScpiStep",
  "version": "0.1",
  "capabilities": [ "scpi", "visa" ],
  "resourceTypes": [ "power", "multimeter" ]
}
```

引擎在启动时读取插件 manifest，构建 capability 索引，调度前匹配 step 所需 capability。

13) Verdict 扩展与 Analyzer 结果映射
-----

目标
- 在 P1 中把 Verdict 扩展为 `Pass` / `Fail` / `Inconclusive` / `Aborted` / `Error`，并把各类 Analyzer（例如 limit-check、regression、slope-check、多点对比器）产出的结果统一映射为 Verdict。确保 Verdict 的汇总规则对 step、cell 与 plan 层都是明确且可配置的。

数据模型与优先级
- 建议定义 C++ 枚举与结果结构：

```cpp
enum class Verdict { Pass, Fail, Inconclusive, Aborted, Error };

struct AnalyzerResult {
  std::string analyzerId;
  bool hasValue;
  double value;           // 可选
  std::string status;     // e.g. "pass"/"warn"/"fail"/"error"/"no_result"
  std::string message;
};
```

- 映射优先级（从高到低）: Error > Aborted > Fail > Inconclusive > Pass。
- 合并规则示例: 如果任一 AnalyzerResult.status == "error" -> Verdict::Error；否则若任一 == "fail" -> Verdict::Fail；否则若任一 == "warn" 或某些 no_value 条件 -> Verdict::Inconclusive；否则 `Pass`。

映射策略与可配置性
- 允许在 Step/Plan 级别声明映射策略（例如某些 analyzer 的 fail 只是 warning，被 map 为 Inconclusive）；通过 step manifest 或 Excel 中的 `verdictPolicy` 字段配置映射优先级或权重。示例配置:

```json
{
  "verdictPolicy": {
    "priority": ["error","fail","warn","no_result","pass"],
    "treatWarnAs": "Inconclusive"
  }
}
```

聚合语义
- Step Verdict: 由该 step 所有 analyzer 的结果按映射策略聚合得出。
- Cell Verdict: 由该 cell 内所有 step Verdict 按优先级归并得出。
- Plan Verdict: 由所有 cell 的 Verdict 按优先级归并得出。

记录与报告
- 每个 step 结果中写入 `step-results.jsonl` 包含 `verdict` 字段与各 `AnalyzerResult` 明细，便于复核与追溯。聚合器在合并时计算并记录最终 Verdict 汇总。

示例代码片段（伪）:

```cpp
Verdict MergeAnalyzerResults(const std::vector<AnalyzerResult>& res,
                             const VerdictPolicy &policy) {
  if (std::any_of(res.begin(), res.end(), [](auto &r){ return r.status=="error"; }))
    return Verdict::Error;
  if (std::any_of(res.begin(), res.end(), [](auto &r){ return r.status=="fail"; }))
    return Verdict::Fail;
  if (std::any_of(res.begin(), res.end(), [](auto &r){ return r.status=="warn" || !r.hasValue; }))
    return Verdict::Inconclusive;
  return Verdict::Pass;
}
```

14) Excel 嵌套步骤与 PrePlanRun/PostPlanRun 钩子（支持阶层编号）
-----

目标
- 让 `XlsxParser` 支持从 Excel 中直接描述嵌套步骤（substeps）与 Plan 级资源声明，支持阶层编号格式（例如 `1`, `1.1`, `1.2.1`），并把这些信息映射到 workflow 树形结构与 lifecycle scope（plan/step/substep/cell）。

Excel 格式建议
- 在主 Steps 表中增加列: `Path`（阶层编号）、`StepId`、`Type`、`Resources`、`Params`、`Scope`。例如:

| Path | StepId | Type | Resources | Params | Scope |
|------|--------|------|-----------|--------|-------|
| 1    | init   | Pre   | PWR1(plan)| {}     | plan  |
| 2    | meas   | Scpi  | METER1    | {...}  | step  |
| 2.1  | meas.a | sub   | CH1       | {...}  | substep |

解析算法
- 按 `Path` 列排序后，逐行插入到树：`1` 是根，`1.1` 的父节点为 `1`，`2.1.1` 的父为 `2.1`。使用分隔符 `.` 分割并依次搜索父节点。
- 若某行未给出 `Path`，回退到线性追加行为以保证向后兼容。

PrePlanRun/PostPlanRun 的 Excel 支持
- 在 Excel 中提供 `Resources` 表或在 Steps 表中通过 `Scope=plan` 显式声明 plan 级资源；`XlsxParser` 在解析时把这些资源收集到 PlanContext，并在执行前调用 PrePlanRun（见 章节“执行模型”）。

示例：从 Excel 到 JSON 的转换片段

```json
{
 "planId":"demo",
 "resources":[{"id":"PWR1","scope":"plan","mode":"exclusive"}],
 "steps":[{ "path":"1","id":"init","type":"Pre","scope":"plan" },
           { "path":"2","id":"meas","type":"Scpi","scope":"step",
             "substeps":[{"path":"2.1","id":"meas.a","type":"sub"}] }]
}
```

兼容与回退
- 保持原有线性 Excel 格式向后兼容；当检测到 `Path` 列时启用树结构解析。

15) OpenTAP 兼容层 / 导出器 与 共享驱动仓库（P3）
-----

目标
- 为了便于迁移与互操作，提供两种方案：
  - 导出器 (Exporter): 把 EonTest 的 Excel/workflow 导出为 OpenTAP 配置/脚本（例如 C# 步骤脚本或 YAML/JSON 配置），便于在 OpenTAP 环境中复现测试；低侵入、风险小。
  - 兼容层 (Compatibility Layer): 实现一个运行时层，把 OpenTAP 的资源/步骤调用映射到 EonTest 的 `ResourceManager`/`IScpiIO`，从而在 EonTest 内运行 OpenTAP 插件（需更多兼容性工作）。

导出器要点
- 输入: `Xlsx` 或 EonTest JSON workflow
- 输出选项:
  - OpenTAP C# step 脚本（每个 EonTest step 生成对应 C# class，资源映射到 OpenTAP 的 `Resource`）
  - OpenTAP YAML 配置文件（如果目标是 config-driven OpenTAP）
- 主要映射工作:
  - 资源标识符映射（EonTest resourceId -> OpenTAP resource registration）
  - Step 类型与参数映射（ScpiStep -> InstrumentCommandStep）
  - Verdict 与 Analyzer 结果映射到 OpenTAP 的 Verdict

共享驱动仓库
- 建议建立一个 vendor-neutral 的驱动仓库，驱动以声明式元数据加实现代码分离：
  - 驱动 manifest 包含 capability、资源类型、配置 schema
  - 提供针对 EonTest 与 OpenTAP 的适配器层，减少重复工作

可行性建议
- 推荐先实现导出器（低风险），验证能将多数 workflow 导出到 OpenTAP 并在该环境运行；随后评估兼容层的成本与回报。

实施时间估算（P3）
- 导出器原型: 2–3 人周
- 兼容层原型: 4–8 人周（视 OpenTAP API 覆盖程度）


10) 测试计划、验收标准与 CI 要求
-----

必测场景 (P0)
- 并发 Acquire/Release 压力测试（100k 次以上）
- 4 个 Cell 同时请求同一台电源（exclusive），断言只有一个成功持有 lease
- scpi.trace.jsonl 结构化产出，每个 cell 生成独立文件，聚合器能合并生成 summary

CI 要求
- 添加 ResourceManager 单元测试（mock IResource），并在 Windows runner 上跑并发测试。
- 提供一个“虚拟仪器”二进制或 python stub，用于 CI 中代替真实硬件。

验收指标
- Acquire/Release 的并发成功率与错误率在压力测试中满足预期（建议: 出错率 <0.01%）
- 每个 cell 的 trace 文件存在且能被聚合器合并

11) P0–P2 分阶段实施计划与时间估算
-----

P0（1–2 人周）
- `ResourceManager` 接口 + 基本实现
- ScpiStep 插件切换到 lease 获取模式
- per-cell Worker 原型（进程模式）与日志隔离实现
- scpi.trace schema 定义与聚合器原型脚本

P1（2–4 人周）
- MatrixManager 与批量路由事务
- 插件 manifest 与 capability discovery
- Studio 并行看板与资源冲突可视化

P2（4+ 人周）
- 分布式 Runner/Agent（跨机器调度）
- 长期遥测接入（Prometheus/Grafana）

12) 下一步（开发任务与命令）
-----

- 我建议现在开始实现 P0 中的 `ResourceManager` 原型并在本地编译验证：
  - 新增: `SDK/include/eon/sdk/ResourceManager.h`
  - 新增: `Runtime/src/ResourceManager.cpp`
  - 修改: `Runtime/src/WorkflowEngine.cpp`（集成 Acquire/Release）

构建（在工作区根目录）:

```powershell
cd E:\SourceCode\EonTest
cmake --build build --target all --config Release
```

我已把该详尽文档写入: [docs/EonTest_System_Architecture_Detailed_Report.md](docs/EonTest_System_Architecture_Detailed_Report.md)

16) 高可用性、死锁检测、插件兼容与可重现性（补充）
-----

下面针对您列出的具体需求给出实现细节与示例，已经补入到本设计文档中并列为独立任务（见 TODO）。

16.1 Lease keepalive 与 agent 死亡回收
- 目的：确保当 agent（或 worker 进程）崩溃/断连时，所持有的 lease 能被及时回收，避免设备僵死或长期占用。
- 数据模型：在 ResourceEntry 中记录 `leaseId, ownerAgentId, lastHeartbeatTs, ttlMs`。
- 心跳机制：Agent 在持有 lease 后以固定间隔（例如 2s）调用 `ResourceManager::RenewLease(leaseId)` 或发送 heartbeat 消息。RenewLease 更新 `lastHeartbeatTs` 并延长有效期至 now + ttlMs。
- 回收逻辑：ResourceManager 定期（例如每 1s）扫描所有 leases，若 now - lastHeartbeatTs > ttlMs + graceMs，则：
  - 标记 lease 为 `expired`；
  - 将对应资源置为 `quarantine` 并尝试安全释放（调用 `IResource->Reset()` 或 `Close()`）；
  - 生成 audit 事件 `lease.expired`，并根据 policy（自动回收 / 人工确认）采取后续动作。

伪代码：

```cpp
void ResourceManager::LeaseHeartbeatLoop() {
  while(running) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto now = Now();
    for (auto &entry: entries) {
      std::lock_guard g(entry.mutex);
      for (auto &lease: entry.leases) {
        if (now - lease.lastHeartbeat > lease.ttl + lease.grace) {
          lease.state = LeaseState::Expired;
          Audit("lease.expired", lease);
          TryRecoverResource(entry);
        }
      }
    }
  }
}
```

16.2 死锁检测与可配置的 preemption 策略
- 目标：发现资源等待链（wait-for graph）中的环并在策略允许时安全预占或回滚，避免系统长时间阻塞。
- Wait-for 图：在 ResourceManager 中维护等待列表，记录 `requester->resource` 边和 `resource->owner` 边，周期性运行环检测（Tarjan 或 DFS）以识别循环依赖。
- 报警与策略：默认行为是记录并报警（audit +报警系统）；可配置 `preemptionPolicy`：
  - `none`：仅报警；
  - `notify`：通知持有者并等待短暂窗口（例如 2s）；
  - `force-preempt`：直接强制回收最老或最低优先级的 lease；
  - `priority-based`：按 caller 的 priority 字段决定抢占目标。

配置示例（resource policy）:

```json
{
  "resourceId": "PWR1",
  "preemptionPolicy": "priority-based",
  "preemptionGraceMs": 2000
}
```

预防策略（减少死锁概率）:
- 资源排序（global ordering）：若所有客户端按约定顺序请求资源，则无法形成循环。
- try-acquire/backoff：使用 `TryAcquire` + 指数退避与随机抖动，避免长期阻塞。
- 监控阈值：对高等待时长触发告警并记录 stacks 与持有者信息以便人工干预。

16.3 在 manifest 中加入 plugin 版本与兼容矩阵
- manifest 格式扩展：在现有 `plugin.json` 中增加 `version`, `apiVersion`, `compatibleEonTest`（semver range）, `dependencies` 和 `compatibilityMatrix` 字段。

示例 manifest:

```json
{
  "name": "ScpiStep",
  "version": "1.2.0",
  "apiVersion": "1.0",
  "compatibleEonTest": ">=1.5.0 <2.0.0",
  "compatibilityMatrix": {
    "eon": {"min":"1.5.0","max":"1.9.9"},
    "opentap": {"min":"9.0.0","max":"9.99.99"}
  },
  "capabilities": [ "scpi", "visa" ]
}
```

加载时校验：Engine 在 plugin load 阶段使用 semver 语义校验（可用现成 semver 库），若不兼容则拒载并记录 reason。支持 `--force-load` 选项供开发环境使用。

16.4 instrument retry/backoff 策略与幂等要求
- 目标：为不稳定的 I/O 操作提供可配置的重试策略，同时要求关键写操作声明幂等性以安全重试。
- 策略参数:
  - `maxRetries`（默认 3）
  - `backoffBaseMs`（例如 50ms）
  - `backoffFactor`（例如 2.0）
  - `jitter`（百分比，例如 0.1）
  - `retryableErrors` 列表（timeout, transient comms errors）

重试包装器（伪代码）:

```cpp
template<typename F>
Result RetryWithBackoff(F op, RetryPolicy p) {
  int attempt = 0;
  while (true) {
    auto res = op();
    if (res.ok() || !IsRetryable(res)) return res;
    if (++attempt > p.maxRetries) return res;
    auto backoff = p.baseMs * pow(p.factor, attempt-1);
    backoff += Random(-p.jitter*backoff, p.jitter*backoff);
    std::this_thread::sleep_for(std::chrono::milliseconds((int)backoff));
  }
}
```

幂等性要求：
- 插件在 manifest 中声明其写操作是否幂等（`idempotentWrites: true/false`）。若非幂等，Engine 在重试前应使用验证（`query()`/`readback`）或避免自动重试，改为人工复核或标记为 `Inconclusive`。

16.5 捕捉运行环境快照（driver/firmware/commit id）以保证可重放性
- 内容项（建议）:
  - `runId`, `timestamp`, `git.commit`（工作区 HEAD 或构建时的 commit）
  - `buildInfo`（编译器版本、CMake args、依赖版本）
  - `os`（平台/版本）
  - `pluginManifestVersions`（加载的 plugin + manifest.version）
  - `resourceInventory`（每个 resource 的标识、驱动版本、firmware 版本（通过 vendor-specific 查询如 `SYST:VER?` / `*IDN?`））
  - `envVars`（必要的环境变量）

实现方式：在 `PrePlanRun` 开始时由 Engine/ResourceManager 收集信息并写入 `reports/run-<id>/env_snapshot.json`。示例收集命令:

```powershell
git rev-parse --short HEAD > commit.txt
echo { "os": (Get-CimInstance Win32_OperatingSystem).Caption } > env_snapshot.json
// 对于仪器，使用 scpi query: scpi.query("*IDN?") / scpi.query("SYST:VER?")
```

验收：每次 Plan 执行目录下包含 `env_snapshot.json`，snapshot 中的关键字段（git commit、plugin versions、设备固件）非空。

-- 结束 --
-- 结束 --
