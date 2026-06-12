# EonTest

Qt6-based architecture skeleton for:

- **Domain layering**: `Core` + `Runtime` + `SDK`.
- **Plugin system**: Runtime loads `IActivityPlugin` implementations dynamically.
- **Workflow engine**: `WorkflowEngine` executes a minimal activity pipeline.
- **Event-driven model**: `EventBus` emits lifecycle events.
- **Multi-process-ready split**: `Orchestrator` and `Studio` are isolated executables.

## Repository layout

```text
Core/          # Domain primitives (event bus)
Runtime/       # Workflow runtime and plugin loading
SDK/           # Public plugin contracts
Orchestrator/  # Headless runtime host (MVP entrypoint)
Studio/        # UI process scaffold
Plugins/       # Sample runtime plugins
```

## Build

Requirements:

- CMake >= 3.24
- C++20 compiler
- Qt >= 6.5 (`Core`)

```bash
cmake -S . -B build -G Ninja
cmake --build build --config Release
```

Run MVP:

```bash
./build/bin/eon-orchestrator ./build/plugins
```

## SemVer policy

- Project version is stored in `VERSION`.
- CMake `project()` version is read from `VERSION`.
- Follow **SemVer** (`MAJOR.MINOR.PATCH`) for releases and tags.
