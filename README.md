# EonTest

Qt6-based architecture skeleton for:

- **Domain layering**: `Core` + `Domain` + `Application` + `Runtime` + `SDK`.
- **Plugin system**: Runtime registers versioned `IStepPlugin` / `IAnalyzerPlugin` / `IReporterPlugin` contracts dynamically.
- **Workflow engine**: `WorkflowEngine` executes step graphs with retry/timeout, conditional branching, parallel-batch primitives, and compensation rollback.
- **Event-driven model**: `EventBus` emits lifecycle events.
- **Multi-process execution**: `Orchestrator` launches `RuntimeWorker` as a child process.
- **Basic observability**: Orchestrator/RuntimeWorker emit structured JSON telemetry events and counters.

## Repository layout

```text
Core/          # Domain primitives (event bus)
Domain/        # Workflow definitions and domain contracts
Application/   # Use-case orchestration layer
Runtime/       # Workflow runtime and plugin loading
SDK/           # Public plugin contracts
Orchestrator/  # Headless runtime host (MVP entrypoint)
RuntimeWorker/ # Runtime execution worker process
Studio/        # UI process scaffold
Plugins/       # Sample runtime plugins
Workflows/     # Workflow DSL samples (JSON)
```

Included sample plugins:

- `sample.activity` (step)
- `sample.analyzer` (analyzer)
- `sample.reporter` (reporter)

## Build

Requirements:

- CMake >= 3.24
- C++20 compiler
- Qt >= 6.5 (`Core`)

```bash
cmake -S . -B build -G Ninja
cmake --build build --config Release
ctest --test-dir build --output-on-failure --build-config Release
```

Run MVP:

```bash
./build/bin/eon-orchestrator ./build/Plugins
```

Run multiple workflow tasks with worker pool:

```bash
./build/bin/eon-orchestrator --cells 2 ./build/Plugins ./Workflows/minimal.workflow.json ./Workflows/parallel.workflow.json
```

Scheduler persistence/recovery and retry policy:

```bash
./build/bin/eon-orchestrator --cells 2 --state ./build/bin/orchestration-state.json --max-task-retries 2 --retry-backoff-ms 300 ./build/Plugins ./Workflows/minimal.workflow.json ./Workflows/compensation.workflow.json
./build/bin/eon-orchestrator --resume --state ./build/bin/orchestration-state.json
```

Stop-on-failure scheduling policy (stop dispatching new tasks after first terminal failure):

```bash
./build/bin/eon-orchestrator --cells 2 --stop-on-failure ./build/Plugins ./Workflows/compensation.workflow.json ./Workflows/minimal.workflow.json
```

Priority scheduling example (higher value first, same priority by FIFO):

```bash
./build/bin/eon-orchestrator --cells 1 ./build/Plugins ./Workflows/parallel.workflow.json ./Workflows/branching.workflow.json
```

Run with workflow DSL file:

```bash
./build/bin/eon-orchestrator ./build/Plugins ./Workflows/minimal.workflow.json
```

Branching example:

```bash
./build/bin/eon-orchestrator ./build/Plugins ./Workflows/branching.workflow.json
```

Recovery policy example (`continue_on_error` + skip transition):

```bash
./build/bin/eon-orchestrator ./build/Plugins ./Workflows/recovery.workflow.json
```

Compensation example (fail-fast + rollback step):

```bash
./build/bin/eon-orchestrator ./build/Plugins ./Workflows/compensation.workflow.json
```

Parallel group primitive example:

```bash
./build/bin/eon-orchestrator ./build/Plugins ./Workflows/parallel.workflow.json
```

Studio visual prototype (workflow list + run controls + live telemetry):

```bash
./build/bin/eon-studio
```

Studio multi-cell monitoring:

```bash
./build/bin/eon-studio --cells 2 ./build/Plugins ./Workflows/minimal.workflow.json ./Workflows/parallel.workflow.json
```

Studio batch mode (CLI monitor, exits on completion):

```bash
./build/bin/eon-studio --batch --cells 2 ./build/Plugins ./Workflows/minimal.workflow.json ./Workflows/parallel.workflow.json
```

Workflow DSL fields:

- `workflowId` (string)
- `priority` (int, optional, default `0`; higher number schedules earlier for runnable tasks)
- `entryStepId` (string, optional; defaults to first step)
- `resourceLocks` (string array, optional; orchestrator-side lock scheduling)
- `initialData` (object, optional)
- `steps[]`:
  - `stepId` (string)
  - `pluginId` (string)
  - `parallelGroupId` (string, optional; steps in same group run as one parallel batch primitive)
  - `conditionKey` / `conditionEquals` (string, optional)
  - `compensationStepId` (string, optional; rollback step invoked on workflow failure)
  - `maxRetries` (int, optional, default `0`)
  - `timeoutMs` (int, optional, default `0`)
  - `failurePolicy` (`fail_fast` | `continue_on_error`, optional, default `fail_fast`)
  - `onSuccessStepId` / `onFailureStepId` / `onSkippedStepId` (string, optional)

## SemVer policy

- Project version is stored in `VERSION`.
- CMake `project()` version is read from `VERSION`.
- Follow **SemVer** (`MAJOR.MINOR.PATCH`) for releases and tags.
