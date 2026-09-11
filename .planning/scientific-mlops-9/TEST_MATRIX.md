# TEST MATRIX — living document

Per-milestone test obligations (all executed locally; "not run" must be
marked explicitly, never counted as PASS — goal §2.2).

| Milestone | Suite | Kind | Status |
|---|---|---|---|
| M0 | test_mlops9_split_validation (new) | regression: #875 + full config-validation matrix (NaN/Inf/negative/overflow/degenerate) | pending |
| M0 | test_mlops9_spatiotemporal (new) | known-answer: joint space×time isolation, block atomicity, determinism | pending |
| M0 | split_leakage (existing) | regression | pending |
| M1 | test_mlops9_version_dag (new) | lineage/diff/tags/pins/round-trip | pending |
| M2 | dataset governance suites (existing + extensions) | governance invariants | pending |
| M3 | test_mlops8_bridge + new lifecycle cases | ghost/fabricated-history rejection, resume args, CLI auto-record | pending |
| M4 | test_experiment_evaluation + new evidence schema cases | known-answer metrics, schema versioning, slice metrics | pending |
| M5 | test_mlops9_matrix (new) | sweep bounds, submission through chain, aggregation, missing-run honesty | pending |
| M6 | comparison extensions | identity/artifact/runtime diff, typed incomparability | pending |
| M7 | reproducibility suites + replay-through-chain case | deviation report, fail-closed | pending |
| M8 | test_mlops9_promotion (new) | criteria, approval metadata, catalog seam | pending |
| M9 | test_mlops9_scale (new) | 100k+ logical stress, concurrent rw, corruption detection | pending |
