# Test ledger — Experiment Exploration Studio

Executable: `test_experiment_studio_core` (Catch2 + Sicnu::experiment_studio)

| Requirement | Test case | Status |
|-------------|-----------|--------|
| valid StudySpec | `experiment_studio valid StudySpec via designer` | PASS |
| invalid StudySpec | `experiment_studio invalid StudySpec ranges` | PASS |
| too-many-combinations | `experiment_studio too-many-combinations refused` | PASS |
| cancellation status | `experiment_studio cancellation status surfaces in matrix` | PASS |
| one failed point | `experiment_studio run matrix one failed point + filter` | PASS |
| deterministic point identity | `experiment_studio deterministic point identity` | PASS |
| report reload | `experiment_studio report reload export roundtrip` | PASS |
| spatial grid mismatch | `experiment_studio spatial grid mismatch typed refusal` | PASS |
| nodata | `experiment_studio spatial nodata handled in buffer summary` | PASS |
| uncertainty missing replicate | `experiment_studio uncertainty missing replicate flagged` | PASS |
| fault sandbox unchanged | `experiment_studio fault sandbox unchanged + diagnosis mismatch` | PASS |
| diagnosis mismatch | same | PASS |
| debugger exact first divergence | `experiment_studio debugger exact first divergence + confidence downgrade` | PASS |
| incomplete evidence → confidence downgrade | same | PASS |
| 100–1000 point table perf baseline | `experiment_studio 1000-point table projection baseline` | PASS (<2s) |
| session roundtrip | `experiment_studio session roundtrip` | PASS |
| offscreen GUI | deferred (qgis_gui weight); dock registered for manual/DoD demo | LIMIT |

Run: `./build/test_experiment_studio_core` — **14 cases / 55 assertions PASS** (2026-09-22 CST).
