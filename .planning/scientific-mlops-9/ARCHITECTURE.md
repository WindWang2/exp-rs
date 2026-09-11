# ARCHITECTURE — scientific-mlops-9

## Principle: extend authorities, never duplicate them

8.0 delivered the execution→experiment bridge. 9.0 turns the recorded truth
into a scientific MLOps platform: versioned data, guarded splits, honest
evidence, matrices, comparison, replay, promotion — all on the existing
stores and execution chain.

## Component map (this track)

```
src/dataset/                       (data authority, owned)
  split.{h,cpp}          M0: validation matrix fix (#875+), SpatioTemporalBlock,
                         split diagnostics/summary hardening
  dataset_store*         M1: version DAG (parents, tags, diff), M2 governance extensions
  leakage_audit/fold_audit  M0: extended audits feeding split summaries

src/experiment/                    (experiment authority, owned)
  run_bridge/run_recorder/  M3: lifecycle hardening tests, CLI/resume
  experiment_store.*          recording surfaces (data-side seams)
  evaluation.*             M4: automatic scientific evidence schema + slice metrics
  experiment_matrix (new)  M5: bounded sweep descriptor → submissions through the
                           existing WorkflowRunCoordinator chain; aggregation
  comparison_ext.*         M6: identity/artifact/runtime diff, typed incomparability
  reproduction_*           M7: replay through existing chain, deviation report
  promotion seam (new)     M8: result→candidate evidence over the model catalog's
                           stable interface (NO new registry)

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
- New `experiment.matrix_*`, `experiment.comparison_incomparable`,
  `experiment.replay_*`, `experiment.promotion_*` families as each
  milestone lands (exact codes in the milestone sections of TEST_MATRIX).
