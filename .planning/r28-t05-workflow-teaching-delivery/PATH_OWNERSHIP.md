# PATH_OWNERSHIP — R28-T05 (track t05-workflow-teaching-delivery)

## T05-owned (primary write)

- `src/workflow/` (workflow backend; NOT `src/app/workflow/` which is T04 canvas)
- `src/experiment/` (identity/recorder/metrics/evaluation/comparison/lineage/reproduction bundles)
- `src/teaching/`, `src/teaching_admin/`, `src/lab_pack/` (teaching experiment flow + errors)
- `src/grader/` (grading engine/matcher/types)
- `src/verify/`, `src/verify_adapters/`, `src/verification/` (verifier lanes)
- teaching/experiment-adjacent `src/explain/`, `src/faultlab/`
- CLI commands for the above: relevant parts of `src/cli/` (cli_lab_commands, experiment/reproduce dispatch)
- `scripts/` validation/generation/install-packaging: `scripts/build_offline_bundle.sh`, snapshot generators, `scripts/check_merge_registration_integrity.py` usage
- `packaging/` (appimage/offline bundle), teaching samples under `data/samples/`, `examples/` (as needed)
- Global: `tests/CMakeLists.txt` governance, generated snapshots under `data/contracts/`, root build/install rules, local smoke integration

## T05 global responsibility (systematic changes allowed, interface notes required)

- Root CMake build/install rules, global test inventory, global snapshot regeneration
- Must leave interface notes for T01–T04 minimal-mechanical taps

## Other tracks (handoff only, no broad fixes)

- T01: algorithm/math (`src/analysis/`, operators numerics)
- T02: JobEngine/TaskCenter/ExecutionPlane, persistence, data storage (`src/data/`, `src/processing/framework/task_center*`, `src/runtime/`)
- T03: planner, agent verifier adapter, security, SDK (`src/sdk/`, `src/agent/` harness/security)
- T04: desktop canvas/UI (`src/app/` incl. `src/app/workflow/`), cartography/atlas (`src/agent/cartography/`)

## Shared hotspots (explicit governance)

- `tests/CMakeLists.txt` — append-only track-tagged blocks; keep all existing targets
- Root `CMakeLists.txt`, core CMake modules
- `data/contracts/*.snap.json` — regenerate with repo generators only (never hand-edit)
- CLI dispatch entry (`src/cli/main_cli.cpp`, `cli_commands.cpp`) — coordinate
- Root README/docs entries touched by delivery docs
