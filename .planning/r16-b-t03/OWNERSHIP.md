# R16-B Ownership

## Track 03 (this worktree) — exclusive write ownership

- `src/science_context/**`
- `src/scientific_state/**`
- `tests/test_science_context_broker.cpp` (extend; already registered)
- `tests/test_science_context_concurrency.cpp` (extend; already registered)
- `.planning/r16-b-t03/**`

No `tests/CMakeLists.txt` change is needed (both test executables are registered at `tests/CMakeLists.txt:14033/14037`), which keeps this track clear of PR #1515's edits to that file.

## Read-only (cross-module)

- `src/agent/data_platform_tools.cpp` — the production `setCapabilityFacts` installer; read to size the race window, not modified.
- `src/recipes/**` — registry behavior read for the static-init verification.
- `src/planner/**`, `src/suitability/**`, `src/preflight/**`, `src/repair_planner/**` — track 04 ownership (separate worktree `exp-rs-r16-b-t04`), never written here.
- `src/help/terminology_provider.cpp` — #1465 sub-item outside both tracks; untouched.

## Integration (third worktree `exp-rs-r16-b-integration`)

Branch `hardening/r16-b-scientific-decisions` receives cherry-picks from both track branches with source SHAs preserved; conflicts resolved against latest `origin/master` semantics, never with `--ours`/`--theirs` blanket overrides.
