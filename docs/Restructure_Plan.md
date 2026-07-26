# EonTest 代码结构重构方案

## 目标

让 EonTest 的代码结构像 OpenTAP 一样"一看就懂"。

## 核心改动

### 1. 添加 `IPlugin` 统一基接口（对标 OpenTAP `ITapPlugin`）

```cpp
// SDK/include/eon/sdk/IPlugin.h  — 新文件
namespace eon::sdk {
class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual QString id() const = 0;
    virtual QString displayName() const { return id(); }
};
```

所有插件接口统一继承自 `IPlugin`：
- `IStepPlugin : IPlugin`
- `IAnalyzerPlugin : IPlugin`
- `IReporterPlugin : IPlugin`
- `IDut : IResource, IPlugin`

### 2. 添加 `StepRun`（对标 OpenTAP `TestStepRun`）

```cpp
// SDK/include/eon/sdk/StepRun.h  — 新文件
struct StepRun {
    QString stepId;
    QString pluginId;
    QDateTime startedAt;
    qint64 elapsedMs = 0;
    Verdict verdict = Verdict::NotSet;
    int attemptCount = 0;
    QString errorMessage;
};
```

### 3. 从 `WorkflowEngine` 剥离 `PluginManager`

把 `loadPlugins()`、`findStepPluginById()` 等移到独立类，WorkflowEngine 只保留执行逻辑。

### 4. SDK 头文件重命名（对齐 OpenTAP 命名规则）

| 现文件名 | → 重命名为 |
|---------|-----------|
| `IActivityPlugin.h` | → `IStepPlugin.h` |
| `AnalyzerResult.h` | → (拆到 StepRun.h + Verdict.h) |
| `RetryPolicy.h` | → 保留，OpenTAP 也有 `RetryPolicy` |
| `TraceEvent.h` | → `ScpiTrace.h` |
| `IResource.h` | → 保留，已对齐 |

### 5. 接口增加 PreExecute/PostExecute 生命周期

```cpp
class IStepPlugin : public IPlugin {
    // 新增：
    virtual void preExecute(WorkflowContext& ctx) {}   // 步骤前调用
    virtual void postExecute(WorkflowContext& ctx) {}  // 步骤后调用（无论成败）
};
```

---

## 实施计划

按依赖顺序分步执行，每步可独立编译验证：

| 步骤 | 改动 | 影响范围 | 工作量 |
|------|------|---------|--------|
| ① | 新建 `IPlugin.h` + `StepRun.h` | 仅新增文件 | 小 |
| ② | `IActivityPlugin.h` → `IStepPlugin.h`（重命名 + 加继承） | SDK + 全部插件 | 中 |
| ③ | 从 WorkflowEngine 剥离 PluginManager | Runtime 模块 | 中 |
| ④ | 给 IStepPlugin 加 preExecute/postExecute | SDK + WorkflowEngine | 小 |
| ⑤ | SDK 其余文件清理对齐 | SDK 模块 | 小 |

要我直接开始实施吗？
