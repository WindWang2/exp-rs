# ADR 0047: Add Asynchronous Plan Execution to AgentWorkflowExecutor

## Status
Accepted

## Context
`executeAgentPlan` was blocking-only (polling `waitForPipeline` up to 60
minutes); `AgentCopilotDockWidget` compensated by spawning a detached
`std::thread` reading UI-owned members — a lifetime/thread-safety hazard.

## Decision
1. **Make `AgentWorkflowExecutor` a `QObject`** (trailing `parent = nullptr`):
   the `TaskCenter::taskUpdated` watcher auto-disconnects on destruction.
2. **Add `executeAgentPlanAsync(planJson, callback, context)`**: returns the
   pipelineId immediately; the callback fires once at pipeline terminal
   state (mirroring `waitForPipeline`), marshaled onto `context`'s thread;
   failures deliver the error-shaped result and return `-1`.
3. **Extract shared parse/normalize and planResult assembly helpers** used
   by both paths — one owner of the result shape; sync stays byte-identical.
4. **Replace the dock's detached thread** with one
   `executeAgentPlanAsync(planJson, callback, this)` call; delete the dead
   `planApprovalRequested` / `toolExecutionFinished` signals.

## Consequences
- **No detached `std::thread` in the dock**; completion runs on the GUI thread.
- **Safe lifetime**: the watcher dies with the executor; a destroyed `context`
  drops its callback instead of touching freed memory.
- **One result-shape owner, no locking**: async and sync share assembly; the
  watcher slot runs on the executor's thread.
