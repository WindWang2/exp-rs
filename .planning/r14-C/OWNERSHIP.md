# R14-C Ownership

## Writable (production code)

- `src/agent/**` — EXCLUDING `src/agent/cartography/**`, `src/agent/mapspec/**`, `src/agent/layout_tools/**` (track D)
- `src/agent_loop/**`
- `src/agent_ops/**` — EXCLUDING `src/app/**` UI (track D); `src/app/teaching_admin/**`, `src/app/experiment_studio/**` are E's
- `src/workflow/**`
- `src/sdk/**`
- `src/plugins/**`
- `src/python/isolated/**` — EXCEPT `python_worker_process_pool.{cpp,h}` + `python_plugin_adapter.cpp` which open PR #1505 modifies → read-only dedupe for this track
- `src/planner/**`, `src/repair_planner/**`
- `pi/**`
- Track tests: new files named `test_r14_C_*` (or edits to track-owned test files only)

## Read-only / other tracks

- `src/runtime/**` (B), `src/geospatial/**` `src/data/**` `src/dataset/**` `src/experiment/**` (B + open PR #1504), `src/app/**` (D/E), `src/science_context/**` (E), `src/operators/rs/**` (A), `src/operators/runtime/**` (B), `src/core/**` (shared; #1505 touches temporal_cube)
- Shared short-lease files (root `CMakeLists.txt`, `tests/CMakeLists.txt`, `.gitignore`, `.github/workflows/**`, global registries): avoid; if unavoidable, minimal block + own commit + explicit ledger note after re-checking other tracks' diffs.
- Master checkout `/home/kevin/project/exp-rs` is read-only for this track (fetch allowed).
- Other agents' worktrees (`exp-rs-hardening-*`, `exp-rs-worktrees/*`, `exp-rs-wt-*`): never touched.

## Dedupe inputs

- Merged: #1477, #1480, #1484 (agent/IPC/workflow), #1500 (planner/repair), #1501 (claims issues #1447–#1495), #1470/#1471 (earlier agent closures).
- Open: #1504 (geodata), #1505 (python/isolated worker pool files + security perimeter).
