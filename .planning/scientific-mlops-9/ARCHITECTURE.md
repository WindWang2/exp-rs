# ARCHITECTURE — scientific-mlops-9

## Principle: extend authorities, never duplicate them

8.0 delivered the execution→experiment bridge. 9.0 turns the recorded truth
into a scientific MLOps platform: versioned data, guarded splits, honest
evidence, matrices, comparison, replay, promotion — all on the existing
stores and execution chain.

## Component map (this track)

```
src/dataset/                       (data authority, owned)
  split.{h,cpp}          M0: total validation matrix (#875 superset),
                         SpatioTemporalBlock, guarded grid arithmetic,
                         bounded generation summaries
  dataset_types.*        M0: SpatioTemporalBlock vocabulary
  dataset_store*         M1: write-time lineage validation, ancestors/
                         children, createDerivedVersion
  sample.*               M2: temporal validity windows (storage + validation)
  (leakage_audit/fold_audit unchanged from 7.0/8.0 — verified sufficient)

src/experiment/                    (experiment authority, owned)
  run_bridge/run_recorder/  M3: CLI/resume recording surfaces (adapter side
  experiment_store.*          in bridge/; data side here), promotion table
  evidence.* (new)         M4: schema-versioned evidence projection + metrics
                           schema versioning (evaluation.*)
  experiment_matrix (new)  M5: bounded sweep descriptor + ledger + aggregation
                           (submission stays with the caller through the
                           existing WorkflowRunCoordinator chain)
  experiment_types.*       M6: RunComparison artifacts+runtime dimensions
  replay_deviation (new)   M7: replay deviation verdicts
  promotion.* (new)        M8: result→candidate evidence over the model
                           catalog's stable interface (NO new registry)
  (comparison_ext.*/reproduction_* unchanged — 7.0/8.0 machinery verified
  sufficient; M7 consumes run comparisons, M6 extends experiment_types)

tests/test_mlops9_*                per-milestone suites; existing suites extended
```

## Key invariants (enforced by code + tests)

1. **Split engine is a pure function**: (config, seed, inputs) → manifest.
   Validation is total: every parameter is checked for NaN/Inf/sign/range
   BEFORE any arithmetic; degenerate datasets are refused with typed
   diagnostics, never silently degraded (#875 class).
2. **Versions are immutable once committed**; everything downstream
   (splits, pins, experiments) references version identity + membership
   digest, never mutable paths.
3. **The recorder never executes**; the matrix never schedules — it
   submits through the existing chain and reads back truthfully.
4. **Comparison surfaces differences; it never hides them.** Incomparable
   runs produce typed reasons, not numbers.
5. **Replay is fail-closed**: missing identity/availability ⇒ typed
   Impossible, never a best-effort impersonation.
6. **Every bounded resource states its bound** (sweep sizes, aggregation
   windows, metadata caps).

## Data flow (9.0 additions in bold)

execution (existing chain) → bridge records truth → **evaluation schema
(M4)** → **matrix aggregation (M5)** → **comparison (M6)** → **promotion
evidence (M8)**; dataset versions + splits flow in as **pinned DAG
identities (M1)**; **replay (M7)** re-enters through the same execution
chain with the recorded pins.

## Diagnostics vocabulary (extends existing)

- `dataset.split_invalid` (existing) with new typed messages for NaN /
  overflow / degenerate folds / spatiotemporal requirements.
- New `experiment.matrix_*`, `experiment.evidence_invalid`,
  `experiment.replay_unknown_run`, `experiment.promotion_*`,
  `dataset.parent_*`, `dataset.version_cycle` families (exact codes live in
  the source and are exercised by the test_mlops9_* suites).
