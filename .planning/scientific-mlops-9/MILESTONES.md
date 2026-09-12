# MILESTONES — living tracker

| Milestone | Scope (goal §6) | State |
|---|---|---|
| M0 | #875 fix + total split config validation (NaN/Inf/neg/overflow), degenerate-fold refusals, spatiotemporal_block, bounded generation summaries | done; verified green (see TEST_MATRIX) |
| M1 | version DAG: write-time parent validation (dangling/cross-dataset/cycle), versionChildren/versionAncestors with corruption refusal, createDerivedVersion | done; verified green (see TEST_MATRIX) |
| M2 | sample temporal-validity windows (round-trip + refusals); existing governance verified from 8.0 evidence | done; verified green (see TEST_MATRIX) |
| M3 | CLI auto-record (8.0 follow-up) via WorkflowExperimentMonitor + --experiment-record/--pin-* surfaces; resume opt-in; truthful-failure E2E; no fabrication for unrecorded runs | done; verified green (see TEST_MATRIX) |
| M4 | evidence.h projector (completeness with typed missing list), metrics schema versioning (MetricRecord + recordMetrics stamping) | done; verified green (see TEST_MATRIX) |
| M5 | experiment_matrix descriptor (bounded cells, deterministic cellId), ledger over store lineage edges, aggregation (missing/failed honesty), pareto with incomparability refusal | done; verified green (see TEST_MATRIX) |
| M6 | RunComparison extended with artifacts + runtime dimensions (1%/1s jitter policy) | done; verified green (see TEST_MATRIX) |
| M7 | replay_deviation analyzer (identical/equivalent/deviated/incomplete verdicts, env drift, no apples-to-oranges metric deltas) | done; verified green (see TEST_MATRIX) |
| M8 | promotion seam (PromotionRecord store table + criteria evaluation + benchmark membership + append-only approval trail; NO registry) | done; verified green (see TEST_MATRIX) |
| M9 | test_mlops9_scale: 100k stress, bounded pages, concurrent readers/writer, corruption refusal, injected commit-fault recovery | done; verified green (see TEST_MATRIX) |
