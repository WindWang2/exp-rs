# ADR 0039: Deepen RsObiaMainWindow Task Execution & Cancellation Seams

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`RsObiaMainWindow` managed asynchronous OBIA background tasks using internal `m_pendingTaskId` and `PendingOp` fields, but did not expose task state or a cancellation method to callers or test suites.

## Decision

1. **Public Execution Queries**: Expose `enum class PendingOp`, `pendingOp()` getter, and public `isBusy()` on `RsObiaMainWindow`.
2. **Unified Cancellation Seam**: Add `cancelActiveTask()`, routing background task cancellation cleanly to `TaskCenter::instance().cancelTask()` and resetting pending UI state.

## Consequences

- **Encapsulation**: External components can observe active OBIA background operations without reaching into private Task Center IDs directly.
- **Testability**: Unit tests can programmatically verify active task status and exercise cancellation handling.
