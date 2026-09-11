# TEST_MATRIX — planned evidence

All tests deterministic, bounded, no network, temp-disk only. "Executed"
columns filled during M1–M6 with the exact commands in PERFORMANCE.md.

## Bridge core (tests/test_mlops8_bridge.cpp, light target)

| Case | Fails-before? | Asserts |
|---|---|---|
| full success lifecycle (Created→Running→Completed) | yes (no wiring exists) | statuses, timestamps order, artifacts digests |
| failure lifecycle stores error evidence | yes | Failed + error in metrics doc |
| cancellation lifecycle | yes | Cancelled + reason |
| interruption + resume continuation (same run id) | yes | Interrupted→Running→Completed |
| duplicate terminal delivery is no-op (idempotent) | — | store state unchanged, no error surfaced |
| unknown state string refused (no guessed transition) | yes | error result, store untouched |
| event for unknown experiment → typed error | — | recorder_missing_experiment |
| pins merge before/after Running (both orders equal record) | yes | dataset/split/model pins + verified fingerprint |
| ensureExperiment idempotent | — | single row |
| stale: live ref untouched | yes | no transition |
| stale: failed/canceled/interrupted checkpoint decisions | yes | truthful closes; interrupted stays resumable |
| stale: missing checkpoint reported only | yes | report-only |
| read-only store → writes fail typed | — | dataset/experiment.store_read_only |
| params/env redaction on recorded run | — | secret keys masked |

## Bridge E2E (tests/test_mlops8_e2e.cpp, coordinator harness)

| Case | Fails-before? | Asserts |
|---|---|---|
| success pipeline → Completed experiment run (auto) | yes | run exists via read APIs; artifacts match step outputs |
| failing executor → Failed experiment run | yes | truthful Failed, error message evidence |
| cancel → Cancelled | yes | truthful Cancelled |
| crash-simulated Interrupted → resume → Completed (same run id) | yes | one run, full story |
| monitor detached → zero experiment writes | yes | store empty |
| untracked/foreign runIds never recorded | — | isolation |

## Surface (M3)

| Case | Asserts |
|---|---|
| run_workflow with recording args records run | experiment:list/inspect see it; read-only tools unchanged |
| run_workflow WITHOUT args byte-identical behavior | no experiment writes |

## Regression sweep (bounded, -j1)

dataset_core, split_leakage, experiment_evaluation, platform7 library,
data_platform_surface, workflow_run_coordinator, mlops8 bridge, mlops8 e2e,
test_dataset_e2e_examples, adversarial_m2 (library-level guard).
