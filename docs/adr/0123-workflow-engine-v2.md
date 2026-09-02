# ADR 0123: Workflow Engine 2.0 (State Lifecycle, Deterministic Cache, Recovery, and Artifact GC)

## Status

Accepted (wired — production entry points drive the v2 surface)

> **Integration status**: **wired** (#662). Production pipeline submissions
> go through `WorkflowRunCoordinator::startTrackedPipeline*` on every entry
> surface — the GUI workflow session controller, the MCP `run_workflow` /
> `resume_workflow` tools, the CLI pipeline runner, and the agent workflow
> executor. Step transitions persist checkpoints, startup recovery scans
> interrupted runs (`recoverAtStartup`), terminal runs sweep their
> intermediates via `ArtifactGC`, and an interrupted run can be resumed from
> its last checkpoint through MCP `resume_workflow` (#697). The TaskCenter
> remains the dispatch source of truth; the v2 run aggregate mirrors it
> additively, so a v2 hiccup cannot regress legacy pipeline behavior.

## Context

Workflow Engine 1.x in `exp-rs` supported basic sequential and DAG pipeline execution via `TaskCenter`, but had several architectural limitations for enterprise and agent-driven workflows:
1. **Simplified Lifecycle**: The state model only supported primitive states without distinction between planning, ready, resource waiting, interrupted, and cancellation phases.
2. **Non-deterministic Fingerprints**: Cache keys for pipeline steps depended on non-standardized JSON formatting, leading to false cache misses when dictionary keys were unordered.
3. **Lack of Resilient Checkpointing**: Interrupted workflows (due to power loss, crashes, or user pauses) required complete restarts from the beginning.
4. **Intermediate Artifact Accumulation**: Multi-step workflows (e.g. radiometric calibration -> atmospheric correction -> OBIA segmentation -> classification) produced large intermediate raster files and auxiliary sidecars (`.tfw`, `.aux.xml`) without a safe garbage collection mechanism.

## Decision

We introduce **Workflow Engine 2.0** with four core architectural components:

### 1. 10-State Lifecycle Model (`sicnu::workflow::WorkflowRunState`)
The workflow execution model formalizes a 10-state finite state machine with strict transition guards:
- **Active / Pending States**: `Created` -> `Planning` -> `Ready` -> `Running` -> `WaitingResource`
- **Interrupt / Cancel States**: `Interrupted`, `Cancelling`
- **Terminal States**: `Canceled`, `Failed`, `Completed` (terminal states reject any outgoing transitions).

State transitions emit observation events and enforce invariant checks before step scheduling.

### 2. RFC 8785 Deterministic Fingerprinting (`sicnu::data::canonicalizeJsonRfc8785`)
Step execution cache keys are computed using RFC 8785 (JSON Canonicalization Scheme / JCS):
- Lexicographical sorting of JSON object keys.
- Deterministic IEEE 754 float/number representation.
- Structured input revision hashing: `makeExecutionFingerprintV2(operatorId, operatorVersion, params, inputRevisions, outputPorts)`.
- Guarantees identical SHA-256 digests across platforms, compilers, and JSON serialization orders.

### 3. Atomic Checkpointing & Recovery (`sicnu::workflow::WorkflowCheckpointManager`)
- **Atomic Persistence**: Checkpoint state files are written to temporary files and atomically renamed (`.json.tmp` -> `.json`) to prevent corruption during crashes.
- **Run Restoration**: Serializes run status, step artifacts, execution timestamps, and error summaries.
- **Interruption Recovery**: `recoverInterruptedRuns()` identifies non-terminal workflow checkpoints on disk and transitions them safely to `Interrupted` state, enabling incremental step-level resumption.

### 4. Intermediate Artifact Lifecycle & Garbage Collection (`sicnu::workflow::ArtifactGC`)
- **Lifecycle Scopes**: Artifacts declare lifetime tags (`TaskTemporary`, `SessionTemporary`, `Persistent`).
- **Sweep Strategy**: `sweepRun()` cleans up unreferenced intermediate files and cascades deletion to sidecar files (e.g., `.tfw`, `.aux.xml`, `.enp`).
- **Protected Outputs**: Final workflow deliverables (`Persistent`) and active checkpoints are strictly preserved from deletion.

## Consequences

- **Determinism**: Workflows can reliably skip already-computed steps via cache hits.
- **Fault Tolerance**: Crashed long-running workflows can resume from the last successful checkpoint rather than restarting.
- **Disk Efficiency**: Large multi-gigabyte intermediate rasters are cleaned up automatically after downstream steps complete.
- **Full Test Coverage**: Dedicated Catch2 test suites (`test_workflow_engine_v2`, `test_workflow_incremental_cache`, `test_workflow_recovery`, `test_workflow_artifact_gc`) verify all state invariants, serialization, cache hits, and GC sweeps.
