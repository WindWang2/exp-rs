# R14-E Ownership & Collision Ledger

## Written paths (this track only)

- `src/teaching_admin/batch_assessment.cpp`, `json_util.h`, `script_adapters.cpp`, `data_pack_manager.cpp`
- `src/lab/session_state.cpp`
- `src/faultlab/fault_expectations.cpp`, `fault_scenario.cpp`
- `src/scientific_state/model_sidecar.cpp`
- `src/preflight/rules.cpp`
- `src/science_context/recipe_router.h` / `.cpp`
- Tests: `tests/test_teaching_batch_discovery_r10.cpp`,
  `tests/test_lab_runtime.cpp`, `tests/test_faultlab.cpp`,
  `tests/test_scientific_state_provenance.cpp`, `tests/test_preflight_rules.cpp`,
  `tests/test_preflight_golden.cpp` (conscious contract-change pin update),
  `tests/test_science_context_broker.cpp` (router concurrency smoke)

## Dropped at push time (dedupe vs open PR #1510, 2026-10-15)

- `src/app/experiment_studio/experiment_studio_dock.{h,cpp}` and
  `tests/test_experiment_studio_dock.cpp` — the model-backed matrix (WP08) was complete,
  RED→GREEN×2'd and reviewed, then dropped: PR #1510 touched the same files with the
  #1482 thread's bounding arm. Zero remaining file overlap with any open PR.
- `.planning/r14-E/**`

## Explicitly NOT touched (dedupe against open PRs / other tracks)

- `src/verification/canonical_json.cpp`, `src/science_context/agent_adapter.cpp`,
  `src/science_context/capability_router.cpp` — OPEN PR #1509 territory.
- `src/experiment/**`, `src/geospatial/**`, `src/data/**`, `src/dataset/**` — PR #1504 territory.
  (#1458's reproduction-bundle P1 lives there → left to #1504 owner.)
- `src/help/terminology_provider.cpp` (#1465 item) — `src/help/**` is not in E's write list.
- `src/app/workflow/**`, `src/app/main_window_vector.cpp` unresolved threads from #1482 — track D territory.
- No edits to root `CMakeLists.txt`, `tests/CMakeLists.txt`, `.github/**`, `scripts/**`, `packaging/**`.
  (No CMake changes were needed: the studio model lives in files already registered, and
  `json_util.h` is a registered header.)

## Shared-file notes

- `src/teaching_admin/batch_assessment.cpp` and `tests/test_teaching_batch_discovery_r10.cpp`:
  no other open PR touches them (checked #1504/#1505/#1506/#1507/#1508/#1509 file lists).
