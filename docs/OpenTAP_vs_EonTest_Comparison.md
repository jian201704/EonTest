# OpenTAP vs EonTest — 测试流程与架构对比

日期：2026-06-19

## 执行摘要
- OpenTAP 为社区驱动的 C# ATE 框架，擅长资源（Resource）生命周期管理与 VISA 式 IO 抽象，适合多厂商仪器与长期维护的测试流程标准化。
- EonTest 以 Excel 三表驱动与 QML HMI 为核心，强调产线与工程师快速建立用例与深度定制化。建议在保留 EonTest 的 Excel/HMI 优势下，吸收 OpenTAP 的核心设计（统一 Resource 管理、IScpiIO 层、自动 SCPI 错误处理、Verdict 扩展），以兼顾易用性与稳健性。

## 核心对比要点

- 语言与生态
  - OpenTAP：C#，社区与现成驱动丰富，便于共享与复用。
  - EonTest：C++/Qt + Excel 驱动，便于本地定制与产线集成。

- 资源生命周期
  - OpenTAP：引擎在执行前自动 Open 所有 Resource，结束时 Close（集中管理）。
  - EonTest：多数插件在 `executeStep()` 内手动 open/close（已添加 `IResource`，但需引擎层统一管理）。

- IO 抽象（VISA 样式）
  - OpenTAP：统一 IScpiIO 抽象，串口/GPIB/LAN 无差异处理。
  - EonTest：已实现 `IScpiIO`、`SerialScpiIO`、`TcpScpiIO`、`VisaScpiIO`，方向一致，但需加强超时、错误检查与线程安全。

- SCPI 错误与自动化
  - OpenTAP：自动执行 *CLS / *ESR? / SYST:ERR?，并将错误映射为异常或 Verdict。
  - EonTest：推荐把这些操作上移到 `IScpiIO` 层，统一处理错误与重试策略。

- 步骤模型与判定（Verdict）
  - OpenTAP：支持嵌套步骤、`PrePlanRun`/`PostPlanRun` 钩子与多级 Verdict（Pass/Inconclusive/Fail/Aborted/Error）。
  - EonTest：当前以线性步骤为主，有 parallelGroup；Verdict 简化为 Pass/Fail/NotSet。

- 用例编写门槛
  - OpenTAP：面向开发者（C# 类库），对非开发人员门槛较高。
  - EonTest：Excel 原生支持，产线/工程师可以低代码编写测试流程。

## 优势与差距速览

- OpenTAP 优势
  - 资源与 IO 管理规范、错误处理完备、驱动生态成熟，适合厂商中立和长期维护。

- EonTest 优势
  - Excel 三表快速建用例、QML HMI 便于产线操作与快速定制。

- 主要差距（建议补强）
  - 引擎层面的 `ResourceManager`（统一 Open/Close）。
  - 把 SCPI 错误检测（*CLS/*ESR?/SYST:ERR?）与超时放到 `IScpiIO` 实现层。 
  - 线程安全的连接池，避免频繁 open/close 带来时序问题。 
  - 更丰富的 Verdict 等级与嵌套步骤支持。

## 优先级建议（可落地路线）

- P0（短期，最高优先级）
  - 实现 `ResourceManager`：在 PlanRun 前自动 Open 所需 `IResource`，Plan 结束后 Close。
  - 在 `IScpiIO` 层实现 *CLS / *ESR? / SYST:ERR? 自动检测与超时处理。

- P1（中期）
  - 实现线程安全的连接复用池（按 `resourceKey` 索引）。
  - 扩展 Verdict（增加 Inconclusive / Aborted / Error），并让引擎输出包含 verdict 字段。

- P2（中长期）
  - 支持步骤嵌套与一次性 Pre/Post 钩子（Excel 支持子步骤编号，如 1、1.1、1.2）。
  - 提供 OpenTAP 兼容导出器（将 Excel 转换为可在 OpenTAP 运行的计划或脚本）。

## 快速实现要点（工程级指引）

- 入口接口
  - `SDK/include/eon/sdk/IResource.h`：扩展为引擎可识别并管理的资源接口。
  - `SDK/include/eon/sdk/IScpiIO.h`：在接口中加入 `deviceClear()`、`query()` 超时参数与 `readError()`。

- 现有实现参考
  - `Infrastructure/src/SerialScpiIO.cpp`、`Infrastructure/src/TcpScpiIO.cpp`、`Infrastructure/src/VisaScpiIO.cpp`：在 `open()` 中设定超时，在 `query()` 中执行 *CLS/*ESR? 验证并返回完整错误信息。
  - `Plugins/ScpiStep/src/ScpiStepPlugin.cpp`：可改为通过 `ResourceManager` 获取/复用连接而非每步 open/close。
  - `Infrastructure/src/XlsxParser.cpp`：保持 Excel 三表映射，但识别 Step 的 ResourceKey，便于引擎预先 Open。

- 连接池
  - 实现按 `resourceKey`（示例：`COM5:115200`、`TCP:192.168.1.2:5025`）索引的线程安全池，API 提供 `acquire(key)` / `release(key)`。

## 参考与链接

- 已读参考：[docs/OpenTAP_vs_TreeATE_vs_EonTest.md](docs/OpenTAP_vs_TreeATE_vs_EonTest.md)
- 相关代码片段示例：`Plugins/ScpiStep/src/ScpiStepPlugin.cpp`、`Infrastructure/src/XlsxParser.cpp`、`SDK/include/eon/sdk/IResource.h`

## 下一步建议

- 我可以立即实现 P0 的 `ResourceManager` 骨架并在本地编译验证；或先实现 `IScpiIO` 层的 *CLS/*ESR? 自动化。
  - 请选择想先做的项：`ResourceManager` / `IScpiIO`。
