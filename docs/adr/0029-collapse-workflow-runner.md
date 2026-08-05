# ADR 0029: Collapse WorkflowRunner Pass-Through into WorkflowRuntime

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`WorkflowRunner` was a shallow static pass-through class (`src/workflow/workflow_runner.h` and `workflow_runner.cpp`) containing a single static method `run(operatorId, params)`. Its sole implementation consisted of creating an `RSOperator` instance via `RSOperatorRegistry::instance().create(operatorId)` and calling `op->execute(params, context)`.

The only caller of `WorkflowRunner` in the entire codebase was `WorkflowRuntime::runStep`. Maintaining a separate header, implementation, build target entry, and single-caller seam added shallowness and friction without providing any abstracting leverage or alternative adapter implementations.

## Decision

1. **Delete `WorkflowRunner`**: Remove `workflow_runner.h` and `workflow_runner.cpp` from `src/workflow/` and delete `workflow_runner.cpp` from `SICNU_WORKFLOW_SOURCES` in `src/workflow/CMakeLists.txt`.
2. **Deepen `WorkflowRuntime`**: Inline operator creation, `RSOperatorContext` stack allocation, and exception translation (`RSOperatorError` $\rightarrow$ `std::runtime_error`) directly inside `WorkflowRuntime::runStep`.
3. **Contract Parity**: Preserve exact exception semantics and caller contracts for synchronous step execution.

## Consequences

- **Locality**: Step execution and operator artifact side-effects concentrate entirely inside `WorkflowRuntime`.
- **Leverage**: Eliminates a single-caller pass-through seam and shrinks include dependencies across the `workflow` module.
- **Testability**: Unit tests cross `WorkflowRuntime::runStep` directly without indirection.
