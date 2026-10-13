# Direction C Goal: Pi/MCP Agent Execution Closure, Recovery & Model Runtime

## Core Objective
Harden and converge existing Pi extensions, MCP SpatialTool, Agent Ops, TaskCenter, Workflow engine, Plugin SDK, Model Runtime, and isolated Python Worker execution.
Zero invention of new product features or second frameworks.
Adhere strictly to Andrej Karpathy's 4 core guidelines:
1. Think Before Coding
2. Simplicity First (YAGNI)
3. Surgical Changes
4. Goal-Driven Execution & Verification (RED -> minimal fix -> GREEN x2 -> negative verification)

## Work Packages (WP-C1 to WP-C12)
- **WP-C1**: Execution Plane State Machine & Task Authority (Ensure queued/running/completing/terminal strict transition, single terminal state, eliminate duplicate publications).
- **WP-C2**: Tool & MCP Schema Consistency (Standardize tool schemas, parameters, typed error codes, timeouts, fail-closed validation).
- **WP-C3**: Workflow Reliability & Dag Journal (WorkflowRunCoordinator error handling, condition execution, cancellation idempotency, eliminate race between releaseRunOwnership and terminal persistence).
- **WP-C4**: TaskCenter Concurrency & Lifecycle (Resolve happens-before gap between worker callback completion, run ownership release, and drainPersists without artificial sleeps).
- **WP-C5**: IPC Channel & Process Boundaries (Partial frames, oversize JSON handling, unknown method responses, double-close immunity, Linux/Unix domain boundary checks).
- **WP-C6**: Python Worker Thread Safety & Restart Budget (Synchronize `isRestarting`, `crashBudgetLeft`, `restartCount` in `PythonWorkerProcessPool` under node mutex / atomic, prevent zombie node leakage on startWorker failure).
- **WP-C7**: Model Runtime Admission & Session Lifecycle (Enforce VRAM accounting bounds, ticket-based in-flight coalescing compatibility, typed failure broadcasting, CancelToken propagation).
- **WP-C8**: Science Context & Typed Evidence Propagation (Ensure cache key includes typed failure reason, prevent stale cache entries, validate CRS/units propagation).
- **WP-C9**: Agent Ops Recovery & Journal Idempotency (Ensure replay idempotency, protect oldest-evidence retention for goal normalization, atomic evidence persistence).
- **WP-C10**: Performance, Event Flow & Resource Containment (Bounded event queues, trace/log buffer memory bounds, RSS monitoring).
- **WP-C11**: CLI / SDK Argument Hygiene & Fail-Closed Gates (Fail-closed argument validation, path traversal prevention, process spawn safety).
- **WP-C12**: Multi-Entrypoint Regression & Verification Suite (Full regression suite pass across all touched targets, verification x2, 0 P0 / 0 P1 independent review).
