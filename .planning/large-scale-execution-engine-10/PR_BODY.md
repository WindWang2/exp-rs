# PR: Large-Scale Execution / External-Memory / Multi-Worker Engine 10.0

> **Local evidence only; no online CI dependency.** All evidence below comes
> from local build/test runs in the track worktree (`build-dev`, Debug,
> `-j2`, `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`).

## Baseline

- Branch `zcode/large-scale-execution-engine-10` off `origin/master` @ `7d78059d1a`.
- Dedupe: open PRs #973/#974/#975 reviewed — #974's `fabric/chunk_plan` is the
  data-fabric query planner, no file-level overlap; the ep7/ep9 execution
  tracks' fixes (locked IO, transient admission, typed cancel, checkpoint
  identity) are consumed as-is and never re-implemented.

## Architecture (ADR 0148)

The scheduling chain `WorkflowRunCoordinator → TaskCenter → JobEngine →
Executor` stays the only one (no second scheduler). 10.0 adds library-level
substrates under it:

1. **Capability contract** (one vocabulary, additive): `RSOperatorMemoryPolicy`
   gains `GlobalReductionStreaming` + `ExternalMemoryStreaming`;
   `RSOperator::streamingHaloPixels()` declares neighborhood radius; mirrored
   into `TaskMemoryClass` defaults, validator closed list, `largeRasterSafe`,
   agent metadata (`execution.haloPixels`), and the tool-catalog
   `largeRasterSafeOnly` filter.
2. **Tile DAG**: `runtime/chunk/ChunkGraph` — source/stage/join/sink nodes on
   `BoundedChunkQueue`s; deterministic row-major partition; typed
   `ChunkPartitionMismatch`; cancel cancels every queue (no parked-waiter
   deadlock); `TileSpec` gains `bandOffset`/`timeIndex`.
3. **Memory planner**: pure overflow-saturating working-set model with the
   Admit → ReduceConcurrency → Spill → Refuse ladder and structured
   need/have reasons; projected into preflight (`resources.tilePlan`).
4. **External memory**: `ScratchRegistry` (budgeted leases, RAII, atomic
   finalize + digest sidecar, stale sweep), `DiskTileStore`
   (self-describing, digest-verified, `ChunkCorruptTile` fail-closed),
   `BoundedWriteGate` (writer backpressure), `TileCheckpointWriter`
   (versioned mid-task checkpoints, fail-closed identity gates).
5. **Scheduler integration**: `wireVramBudgetFromDeviceTruth` consumes the
   Model Runtime's NVML inventory (no second GPU detector); execution
   fingerprint contract v3 mixes environment pins (GDAL release) into
   implementation identity.
6. **Worker safety**: detection NMS/dedup poll the task's cancel flag (#971).

## Major deliverables

| Commit | Content |
|---|---|
| 8b01887f03 | memory-policy grades + halo declaration + projections |
| a4500cb248 | ChunkGraph + tile identity + memory planner + reduction primitives |
| 1bae4030c8 | scratch leases + disk tile store + tile checkpoint + tests |
| 078ef8d27a | preflight tilePlan + NVML vram bridge + fingerprint env pins v3 |
| 2b77b2febf | #971 NMS/dedup cooperative cancellation |
| 827dcd58d1 | catalog largeRasterSafeOnly filter alignment + env-pin test |
| 3a8bc47ab1 | scale/failure test matrix + ADR 0148 |

## Compatibility

- All new operator-facing surface is opt-in with legacy defaults: existing
  operators keep FullRaster policy, halo 0, byte-identical metadata (pinned
  by tests). No operator was re-classified.
- Fingerprint contract bump v2→v3 invalidates prior cache entries once
  (documented semantic of the contract version) — the cache is opt-in and
  off by default.
- Worker protocol untouched; no wire change.

## Tests

(filled at Phase 8 with final-HEAD runs)

## Performance / resource evidence

(filled at Phase 8)

## Review findings

(filled at Phase 7/8)

## Known limitations / follow-ups

- Grid-indexed NMS (O(n²)→O(n·k)) is a follow-up: cancellation now bounds
  responsiveness; exact-output-equivalent grid bucketing deserves its own
  review (#971 note).
- Operator adoption of ChunkGraph/scratch is deliberately incremental
  (contract + substrate first; no scientific kernel rewrites in this track).
- Scratch budget is per-registry; a future step can wire it to a CLI/env
  knob surface.
