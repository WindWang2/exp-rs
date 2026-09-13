# ADR 0148: Large-Scale Execution Engine 10.0 — Tile DAG, Memory Planner, External Memory & Multi-Worker Hardening

- Status: Accepted (Large-Scale Execution / External-Memory / Multi-Worker Engine 10.0)
- Context: The execution spine (`WorkflowRunCoordinator → TaskCenter → JobEngine →
  Executor`, ADR 0144) schedules arbitrary DAGs with multi-dimension admission,
  bounded auto-retry, worker containment and a content-addressed execution cache.
  Two gaps remain for very large remote-sensing runs. First, the streaming
  substrate is a linear tile chain (`runtime/chunk`) with no way to express a
  multi-input tile DAG, band/time sub-setting, tile lifetime, or which operators
  need halo/global-reduction/external-memory — so admission cannot reason about a
  streaming task's true working set and every intermediate must stay in RAM.
  Second, worker lifecycle lacks a poison-task escalation, and long tasks cannot
  resume mid-execution (only workflow steps can).
- Decision:
  1. **Capability contract stays ONE vocabulary.** `RSOperatorMemoryPolicy`
     gains `GlobalReductionStreaming` (two-pass with a pass-1 global statistic)
     and `ExternalMemoryStreaming` (bounded RAM + declared scratch spill);
     `RSOperator::streamingHaloPixels()` (default 0) declares the neighborhood
     radius. Everything projects additively: `memoryPolicyName`, the
     `TaskMemoryClass` mirror with conservative defaults (128 MiB), the
     descriptor validator's closed list, `largeRasterSafe` derivation, and
     `execution.haloPixels` in agent metadata. No operator is re-classified by
     this ADR; existing defaults are unchanged.
  2. **Tile DAG as a ChunkGraph beside ChunkPipeline.** `runtime/chunk` gains
     a graph runner over value-type nodes (source / stage / join / sink)
     joined by `BoundedChunkQueue`s: an N-input join drains one tile from each
     bounded input per output tile (fan-in with natural backpressure,
     deterministic row-major partition from a grid spec; single-consumer
     nodes — fan-out must go through an explicit copy stage), `TileSpec`
     gains optional `bandOffset`/`timeIndex` (additive, zero-cost defaults).
     STAGED: the tile-payload scratch-reference form ships with the first
     external-memory operator adoption; this ADR's substrate (lease, store,
     checkpoint) is the contract it will use. The existing linear
     `ChunkPipeline` keeps its role for single-input chains; `ChunkGraph`
     generalizes it for multi-input DAGs. Any input closing or cancelling
     drains downstream in bounded time (no deadlocks — same contract family
     as the existing queue).
  3. **Memory planner before admission, refusal with estimates.** A pure
     planner converts (tile geometry, halo, band count, stages, queue
     capacities, dtype) into a working-set estimate and a recommended
     concurrency/queue-capacity shape; TaskCenter admission consults it for
     streaming tasks the way it consults `estimateExecution` today. An
     over-budget plan REDUCES stage concurrency first, then spills (external-
     memory operators), and only then refuses — with a structured
     need/have/Action payload in the hold reason and task log, never as
     `std::bad_alloc` control flow.
  4. **Scratch is a lease registry, not a second storage engine.**
     `ScratchRegistry` issues byte-budgeted leases per run, names temp files
     deterministically, supports atomic finalize (same fsync/rename family as
     the workflow checkpoint), reference-counted release and crash-sweep of
     unowned entries; `ScratchStore` persists tile payloads with a
     self-describing header (magic/version/digest) and a bounded writer queue
     so disk backpressure behaves like queue backpressure. Scratch bytes join
     the existing `tempDiskMb` admission dimension.
  5. **Long-task checkpoint/resume reuses the workflow checkpoint
     primitives.** A tile-loop operator records a versioned, digest-stamped
     checkpoint every N tiles (atomic temp+rename). Resume is fail-closed:
     operator identity / parameter drift / header mismatch / corrupt file all
     re-execute from scratch; a user cancel and a crash are distinguishable
     (the same run-lock ownership rule as #727).
  6. **Scheduler integration without a second GPU truth.** The vram admission
     dimension may be fed from the Model Runtime's NVML-backed device report
     (`wireVramBudgetFromDeviceTruth`, an injectable, host opt-in seam — the
     gate stays default-off per the conservative-defaults rule); scratch
     budget rides `tempDiskMb`. Poison-task convergence stays where 7.0
     bounded it: the transient auto-retry cap (0..3, infrastructure error
     classes only) makes a worker-crashing task reach a terminal Failed after
     exactly the budget — 10.0 pins that convergence with a storm test
     instead of adding a second escalation mechanism.
  7. **Scale tests prove complexity, not wall-clock.** A gated
     (`SICNU_LSEE10_STRESS`) synthetic suite covers: wide fan-out joins,
     worker crash storms, bounded scratch under a 10^6 logical-tile stream,
     checkpoint restart mid-stream, cache hit/miss at scale, and admission
     aging under starvation — asserting bounded resident state and drain
     invariants rather than milliseconds.
- Consequences: very large workflows no longer depend on intermediates being
  resident; admission has a defensible per-task memory story for streaming
  work; halo/global-reduction/external-memory operators become first-class
  scheduling citizens with zero change to existing operators; worker crashes
  converge to typed outcomes; all new surfaces are additive and default to
  master's current behavior.
