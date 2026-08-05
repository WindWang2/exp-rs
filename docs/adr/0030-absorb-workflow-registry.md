# ADR 0030: Absorb WorkflowRegistry into WorkflowRuntime

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`WorkflowRegistry` was a shallow in-memory wrapper around `std::unordered_map<std::string, WorkflowDefinition>` protected by a `std::mutex`. Every component initializing a `WorkflowRuntime` (including `RsGeoreferencingSession`, `WorkflowSessionController`, `RsClassifyWorkflowBridge`, and unit test suites) was forced to create and manage a separate `WorkflowRegistry` instance to inject into `WorkflowRuntime`'s constructor.

This mandatory dual-object boilerplate (`WorkflowRegistry` + `WorkflowRuntime`) added shallowness, constructor injection friction, and object order lifetime fragile coupling without providing an alternative registry implementation.

## Decision

1. **Delete `WorkflowRegistry`**: Remove `workflow_registry.h` and `workflow_registry.cpp` from `src/workflow/` and delete `workflow_registry.cpp` from `SICNU_WORKFLOW_SOURCES` in `src/workflow/CMakeLists.txt`.
2. **Deepen `WorkflowRuntime`**: Add definition management methods (`registerDefinition`, `findDefinition`, `hasDefinition`, `registeredDefinitionIds`) directly to `WorkflowRuntime`.
3. **Auto-Register Built-ins**: Auto-register standard built-in definitions (`lab.georef.image_to_map`, `lab.classify.supervised`, etc.) in `WorkflowRuntime`'s constructor by default (`loadBuiltins = true`).
4. **Caller Simplification**: Callers instantiate `WorkflowRuntime` directly without juggling a separate `WorkflowRegistry` object.

## Consequences

- **Locality**: Definition lookup, built-in definitions, and session orchestration reside together in `WorkflowRuntime`.
- **Leverage**: Callers instantiate a single deep module (`WorkflowRuntime`) directly, eliminating dual-object setup boilerplate.
- **Testability**: Unit tests and test suites require only a single `WorkflowRuntime` instance.
