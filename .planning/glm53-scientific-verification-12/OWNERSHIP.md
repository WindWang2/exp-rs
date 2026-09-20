# OWNERSHIP — glm53-scientific-verification-12

File-level ownership for this track. The main owner is
`src/contracts/**` + `data/contracts/**` + new `tests/verification12/**`
and `benchmarks/verification12/**` + `docs/verification/**` (verification
docs only). Everything else defaults to **read-only**.

## WRITABLE (this track owns)

| Path | Kind | Notes |
|---|---|---|
| `src/contracts/determinism_census.{h,cpp}` | extend | Additive fields only; must keep 11.0 gates green |
| `src/contracts/scientific_contract.{h,cpp}` | extend | Additive vocab/records; never remove/rename existing |
| `src/contracts/CMakeLists.txt` | append-only | Append new sources |
| `src/contracts/tool/contract_inventory_main.cpp` | extend | Additive CLI flags only |
| `data/contracts/*.json` | new/extend | Snapshots + new exemption/oracle data files |
| `tests/verification12/**` | NEW dir | All 12.0 test sources |
| `benchmarks/verification12/**` | NEW dir | Scale-evidence workloads |
| `docs/verification/VERIFICATION_120.md` + siblings | NEW | 12.0 docs |
| `docs/verification/READINESS.{md,json}` | regenerate | Regenerated, not hand-edited |
| `scripts/verification_ladder.py` | extend | Append-only capability rows |
| `scripts/collect_readiness.py` | extend | Append-only rows |
| `CHANGELOG.md` | append-only top entry | |
| `.planning/glm53-scientific-verification-12/**` | NEW | This track's planning |
| `.goal-loop-ledger.md` | ledger | Worktree-local by default |

## SHARED — APPEND-ONLY (touch minimally, expect rebase)

| Path | Rule |
|---|---|
| `tests/CMakeLists.txt` | **append** `sicnu_add_test(...)` blocks at the end of the existing test block; never reorder |
| `.gitignore` | append-only |
| `docs/verification/READINESS.json` | regenerate via script only |

## READ-ONLY (do not modify)

| Path | Why |
|---|---|
| `src/operators/**` | parallel owner; production bug → minimal repro + issue |
| `src/processing/**` | parallel owner; same rule |
| `src/analysis/**` | parallel owner |
| `src/agent/**` | parallel owner (cartography registers operators) |
| `src/workflow/**`, `src/execution/**` | parallel owner |
| `src/geospatial/**`, `src/data/**` | parallel owner |
| `src/plugin/**`, `src/sdk/**` | parallel owner |
| `CMakeLists.txt` (root), `CMakePresets.json`, `cmake/**` | do not restructure |

### Escape hatch (per track brief)

If a **one-line / small seam** production defect is found that (a) blocks a
12.0 oracle and (b) does **not** collide with a parallel owner's active file,
it may be fixed in place **with an explicit note in DECISIONS.md and the PR
body**. Anything larger → record a failing oracle + open an issue; do **not**
make a cross-track large change.

## Concurrency discipline

- **Only the main agent compiles.** Subagents are for source audit, design,
  test design and review — never concurrent heavy builds.
- **This is an independent worktree.** Its `.git` is shared with the main
  repo and other worktrees; only ever `git`-operate on this worktree's paths.
- A **concurrent live sibling track** `agent/glm53-mission-workbench-12` exists
  in another worktree. It may write to shared append-only files. Expect rebase.
- Compile budget: `-j1` default, `-j2` max. Never `-j3+`.
