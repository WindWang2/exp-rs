# MILESTONES — living tracker

| Milestone | Scope (goal §6) | State |
|---|---|---|
| M0 | #875 fix + total split config validation (NaN/Inf/neg/overflow), degenerate-fold refusals, spatiotemporal_block, bounded generation summaries | code done; verification queued |
| M1 | version DAG: write-time parent validation (dangling/cross-dataset/cycle), versionChildren/versionAncestors with corruption refusal, createDerivedVersion | code done; verification queued |
| M2 | sample temporal-validity windows (round-trip + refusals); existing governance verified from 8.0 evidence | code done; verification queued |
| M3 | CLI auto-record (8.0 follow-up) via WorkflowExperimentMonitor + --experiment-record/--pin-* surfaces; resume opt-in; truthful-failure E2E; no fabrication for unrecorded runs | code done; verification queued |
| M4 | evidence.h projector (completeness with typed missing list), metrics schema versioning (MetricRecord + recordMetrics stamping) | code done; verification queued |
| M5 | experiment_matrix descriptor (bounded cells, deterministic cellId), ledger over store lineage edges, aggregation (missing/failed honesty), pareto with incomparability refusal | code done; verification queued |
| M6 | RunComparison extended with artifacts + runtime dimensions (1%/1s jitter policy) | code done; verification queued |
| M7 | replay_deviation analyzer (identical/equivalent/deviated/incomplete verdicts, env drift, no apples-to-oranges metric deltas) | code done; verification queued |
| M8 | promotion seam (PromotionRecord store table + criteria evaluation + benchmark membership + append-only approval trail; NO registry) | code done; verification queued |
| M9 | test_mlops9_scale: 100k stress, bounded pages, concurrent readers/writer, corruption refusal, injected commit-fault recovery | code done; verification queued |
