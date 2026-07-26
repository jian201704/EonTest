EonTest 系统级架构对比与改进计划
=================================

日期: 2026-06-19

执行摘要
-----
- 本文档对比了 EonTest 与 OpenTAP 在系统级（资源管理、并行测试/多 CELL、日志/报告、插件模型、分布式运行、UI/运维、测试/验证）方面的主要差异，梳理出可借鉴要点，并给出分层（P0/P1/P2）改进计划与实现建议。
- 建议优先级：P0（短期必须）——实现统一的 `ResourceManager`（租赁/独占）、把连接池升级为可租赁/加锁、实现 per-cell worker（隔离日志/回退），并把 `IScpiIO` 的自动错误处理上移。

当前快照（EonTest）
-----
- **资源与 IO 抽象**: 已实现 `IResource`（[SDK/include/eon/sdk/IResource.h](SDK/include/eon/sdk/IResource.h)）、`IScpiIO`（[SDK/include/eon/sdk/IScpiIO.h](SDK/include/eon/sdk/IScpiIO.h)）与 `ScpiInstrument`（[SDK/include/eon/sdk/ScpiInstrument.h](SDK/include/eon/sdk/ScpiInstrument.h)）。
- **IO 后端**: 已有 `SerialScpiIO`（[Infrastructure/src/SerialScpiIO.cpp](Infrastructure/src/SerialScpiIO.cpp)）、`TcpScpiIO`（[Infrastructure/src/TcpScpiIO.cpp](Infrastructure/src/TcpScpiIO.cpp)）与基本 `VisaScpiIO`（[Infrastructure/src/VisaScpiIO.cpp](Infrastructure/src/VisaScpiIO.cpp)，超时/健壮性仍需硬化）。
- **Excel 驱动工作流**: `.xlsx` 解析器已实现（[Infrastructure/src/XlsxParser.cpp](Infrastructure/src/XlsxParser.cpp)），能把工作表映射到步骤参数。
- **通用 SCPI 步骤**: 已有 `ScpiStepPlugin`（[Plugins/ScpiStep/src/ScpiStepPlugin.cpp](Plugins/ScpiStep/src/ScpiStepPlugin.cpp)），使用连接池并写入 `scpi.trace` 到运行事件（已在 [Runtime/src/WorkflowEngine.cpp](Runtime/src/WorkflowEngine.cpp) 中集成 `scpi.trace`）。
- **现存 caveats**: 连接池缺乏线程安全（无 mutex/租赁）；`ResourceManager`（引擎级别自动 Open/Close）尚未实现；`VisaScpiIO` 需改进超时与读循环。

OpenTAP 的关键做法（可借鉴）
-----
- 自动的资源生命周期（注册、租用/独占、引用计数、自动回收）
- 清晰的并行模型（Runner/Agent，支持本地与远程并行）
- 插件能力发现与声明（插件元信息描述支持的设备能力）
- 结构化事件/日志（时间序列事件、可机器解析的 JSONLines），便于实时看板与离线聚合
- 强调 per-test 与 per-resource 的错误隔离与恢复策略（可配置的 retry/skip/quarantine 策略）

逐项对比与建议（系统级视角）
-----

资源生命周期
- 差异: OpenTAP 自动管理 `Resource` 生命周期并支持租赁/独占；EonTest 目前靠插件显式 open/close 与连接池复用。
- 风险: 并发调用时可能出现重复打开、交叉关闭或资源泄漏。
- 建议: 设计 `ResourceManager`：支持 `Lease`（独占/共享）、引用计数、超时回收与审计日志。将 `WorkflowEngine` 的资源访问改为通过 `ResourceManager::Acquire(resourceId, mode)`。

并行执行与多 CELL
- 差异: OpenTAP 有并行 Runner/Agent 模型；EonTest 需要明确 per-cell worker（线程或进程）和分布式 agent 方案。
- 要点: 为每个 CELL 提供独立执行环境、独立日志/数据目录与心跳/监控；共享资源通过 `ResourceManager` 租赁。
- 建议: 实现 `CellWorker` 进程/线程模型、心跳 + watchdog、并配合调度器预分配关键资源（power supply、matrix）。

日志（`scpi.trace`）与报告
- 差异: OpenTAP 社区倾向结构化事件（JSON），易于聚合；EonTest 现有 `scpi.trace` 已生成但需统一格式与 per-cell 分离。
- 建议: 采用 JSON Lines（每行一个事件），为每个 CELL 生成独立 trace 文件（如 `reports/cell-<id>/scpi.trace.jsonl`），运行结束由聚合器合并并生成最终 CSV/JSON/HTML 报表。保证日志写入线程安全并采用文件轮转。

插件模型与能力发现
- 差异: OpenTAP 插件登记者会声明 capabilities；EonTest 插件机制缺少统一能力描述与 sandbox。
- 建议: 要求插件提供 manifest（支持的 capability 标签，如 `scpi`, `visa`, `matrix-control`），引擎在调度前做能力匹配，避免运行时错误。

硬件拓扑、矩阵與路由
- 要点: 多 CELL 测试常通过交换矩阵共享测量设备。应引入 `MatrixManager` 抽象用于路由/虚拟化，避免每个步骤直接硬编码路由逻辑。

分布式与扩展性
- 差异: OpenTAP 更容易将 Runner 分布到远端；EonTest 目前以单机为主。
- 建议: 规划 `Agent`（远端 worker）接口，支持 RPC 或 gRPC 调度；在后续（P1）支持容器化部署。

UI 与运维
- 建议: Studio 提供多 CELL 仪表盘（总览 + 单 CELL 详情），支持批量启动/停止/中断与资源冲突可视化。增加运行时警报/告警功能。

测试、回归与指标
- 建议: 引入针对并发与资源争用的集成测试（模拟多个 CELL 并发访问同一资源），并在 CI 中跑 P0 测试集合。

建议借鉴清单（简短）
-----
- 资源租赁/引用计数（必做）
- 每 CELL 独立 worker + 日志隔离（必做）
- 结构化事件（JSON Lines）与后端聚合（必做）
- 插件能力声明（建议）
- 矩阵/路由抽象（建议）
- 分布式 Runner/Agent（中期）

改善计划（优先级与任务划分）
-----
P0（立即，可在数天到两周内交付）
- `ResourceManager`（接口设计、实现、与 `WorkflowEngine` 集成）
  - 新文件: `SDK/include/eon/sdk/ResourceManager.h`，实现: `Runtime/src/ResourceManager.cpp`。
  - 要点: `Acquire(resourceId, mode {shared|exclusive}, timeout)` 返回 `Lease` 对象，`Lease` 在析构/显式释放时释放资源。
- 连接池升级为租赁/加锁
  - 修改: `Plugins/ScpiStep/src/ScpiStepPlugin.cpp` 使用 `ResourceManager` 获取 `IScpiIO` 实例而非直接复用全局池。
  - 增加 mutex/condition 用于并发等待。
- per-cell worker 与日志隔离
  - 改造 `WorkflowEngine` 支持 `CellWorker` 实例（线程或独立进程），每个 worker 写入 `reports/cell-<id>/` 目录。
- `IScpiIO` 自动错误处理（CLS/ESR?/SYST:ERR?），超时统一化
  - 修改: `IScpiIO` 接口增加 `Lease` / `Acquire` 支持并实现统一 `query()` 超时模式与错误回读。
- 验证: 单元 + 集成测试（并发访问、资源冲突、scpi.trace 写入）

P1（中期，数周）
- 矩阵/路由管理抽象（`MatrixManager`）
- 插件 manifest 与 capability 注册机制
- Studio UI：并行看板、资源冲突提示
- `VisaScpiIO` 健壮化（超时、动态加载、读缓冲策略）

P2（长期）
- 分布式 Runner/Agent（跨机器并行）
- 集群调度器与作业队列（优先级、租期、回收）
- 长期遥测/时序数据库接入（Prometheus + Grafana）

实施细节（P0 示例实现建议）
-----
1) `ResourceManager` 最简接口 (C++ 概念示例)：

```cpp
class ResourceManager {
public:
  struct Lease { std::string resourceId; bool exclusive; ~Lease(); void Release(); };
  std::unique_ptr<Lease> Acquire(const std::string &resourceId, bool exclusive, std::chrono::ms timeout);
};
```

插件调用流程：
- `auto lease = ResourceManager::Acquire("VISA::GPIB::1", true, 5s);`
- 插件通过 `lease->GetIScpiIO()` 获得 `IScpiIO` 接口并调用 `query()`。

2) 日志方案：
- 每个 worker 写入 `reports/cell-<id>/scpi.trace.jsonl`，事件包含 timestamp、resourceId、direction、payload、stepId、threadId。聚合器读取所有 JSONL 生成 `reports/summary-<run>.json` 与 CSV。

3) 验证用例：
- 并发 4 个 CELL 同时请求同一台电源（exclusive 模式），断言只有一个 lease 成功，其他等待或失败并按策略重试。

验收标准
-----
- P0 完成当且仅当：
  - `ResourceManager` 能成功按 exclusive/shared 模式分配资源並在超时后回收。
  - 并发测试中无资源泄漏（运行 1M 次 Acquire/Release 的压力测试失败率 <0.01%）。
  - 每个 CELL 产生日志目录，`scpi.trace.jsonl` 为结构化 JSON Lines，聚合器能生成最终报告。

时间预估（粗略）
-----
- P0 合计: 1–2 人周（包含实现 + 基础测试）
- P1 合计: 2–4 人周（含 UI 改造 + 矩阵抽象 + Visa 健壮性）
- P2 合计: 4–8 人周（分布式 Runner 与长期监控）

风险与缓解
-----
- 风险: 资源回收不彻底导致设备状态不确定。缓解: 强制 lease 超时回收 + 在回收时做设备级复位（可选）。
- 风险: 并行日志合并导致 I/O 瓶颈。缓解: 每 CELL 单文件、异步刷盘、后端批量聚合。

下一步建议
-----
- 我可以现在开始实现 P0 的 `ResourceManager` 原型并在本地编译验证：
  - 修改点: `SDK/include/eon/sdk/` 新增 `ResourceManager.h`，`Runtime/src/ResourceManager.cpp` + `Runtime/src/WorkflowEngine.cpp` 集成点。
- 请确认是否现在开始实现 `ResourceManager`（我会先提交小的 PR 样板并运行单元测试）。

参考文件
-----
- 已存在实现與参考: [SDK/include/eon/sdk/IResource.h](SDK/include/eon/sdk/IResource.h), [SDK/include/eon/sdk/IScpiIO.h](SDK/include/eon/sdk/IScpiIO.h), [Plugins/ScpiStep/src/ScpiStepPlugin.cpp](Plugins/ScpiStep/src/ScpiStepPlugin.cpp), [Infrastructure/src/XlsxParser.cpp](Infrastructure/src/XlsxParser.cpp), [Runtime/src/WorkflowEngine.cpp](Runtime/src/WorkflowEngine.cpp)

-- 结束 --
