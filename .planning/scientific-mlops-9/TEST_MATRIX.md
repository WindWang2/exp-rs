# TEST MATRIX — living document

Per-milestone test obligations (all executed locally; "not run" must be
marked explicitly, never counted as PASS — goal §2.2).

| Milestone | Suite | Kind | Status |
|---|---|---|---|
| M0 | test_mlops9_split (new) | regression: #875 + full config-validation matrix (NaN/Inf/negative/overflow/degenerate folds) + spatiotemporal_block + bounded summaries | code written; build queued |
| M0 | test_split_leakage (existing) | regression (pre-existing suite must stay green) | pending re-run |
| M1 | test_mlops9_version_dag (new) | parent validation, ancestor walk + corruption detection, derive/fork | code written |
| M2 | test_mlops9_version_dag (validity section) | sample temporal-validity windows: round-trip + refusals | code written |
| M3 | test_mlops9_cli_record (new) | REAL CLI subprocess: --experiment-record Completed/Failed truthfulness, no fabrication for unrecorded runs | code written |
| M3 | test_mlops8_bridge (existing) | ghost/fabricated-history rejection (8.0 suite stays green) | pending re-run |
| M4 | test_mlops9_evidence (new) | evidence completeness honesty, metrics schema version | code written |
| M5 | test_mlops9_evidence (matrix section) | bounds/ledger/aggregation/pareto/incomparability | code written |
| M6 | RunComparison extension (artifacts+runtime dims) | covered in test_mlops9_evidence replay section + existing comparison suite | code written |
| M7 | test_mlops9_evidence (replay section) | identical/deviated/incomplete verdicts, typed unknown-run | code written |
| M8 | test_mlops9_evidence (promotion section) | criteria, benchmark gap, approval metadata, append-only trail | code written |
| M9 | test_mlops9_scale (new) | 100k stress, bounded pages, concurrent rw, corruption refusal, injected commit fault | code written |
