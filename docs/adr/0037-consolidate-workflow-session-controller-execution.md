# ADR 0037: Consolidate WorkflowSessionController Execution & Task Center Signals

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`WorkflowSessionController` previously managed single-operator and pipeline execution state (`m_pendingTaskId`, `m_activePipelineId`, `m_runInFlight`) with internal state fields that callers could not inspect.

## Decision

1. **Encapsulate Execution State**: Add `isRunInFlight()`, `activeSessionId()`, `activeStepId()`, `activePipelineId()`, and `pendingTaskId()` getters to `WorkflowSessionController`.
2. **Unified Cancellation**: Add `cancelActiveRun()`, consolidating single-task and pipeline cancellation in one primary method and aliasing `stopWorkflow()` to it.
3. **Task Center Integration**: Maintain clean signal-driven session synchronization when `sicnu::TaskCenter::taskUpdated` fires.

## Consequences

- **Inspectability**: Shell and lab window controllers can inspect workflow execution state and task IDs directly.
- **Maintainability**: Cancellation logic is unified in a single method.
