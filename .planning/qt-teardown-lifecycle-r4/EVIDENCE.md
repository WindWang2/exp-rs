# EVIDENCE — Track 2: Qt/QGIS teardown lifecycle (R4 Deep)

Append-only evidence chain. Every claim links to a command + observed output (or commit).

## 1. Baseline inventory (round 0–1)
- `git rev-parse origin/master` → `15e5c66b543ef3874cb929f17529ef456bd6c059`, local master `0/0` vs origin.
- `rg -n "std::_Exit|_Exit\(" src tests` → 43 lines / 28 files / 30 real call sites; `src/` = 0. Classified: 21 stats-variant atexit + 5 ok-variant atexit + 3 crash-injection (`test_chunk_resume_11`) + 1 probe exit (`offline_probe.h:254`).
- `gh issue list --state open` → empty. PRs #1334–#1338 inventoried (BASELINE §2).

## 2. Red-step evidence (crash reproduction) — PENDING BINARIES
Expected procedure (fill in when build lands):
1. Baseline binary `test_gcp_contains` with defense → run → green exit 0.
2. Disable `std::_Exit` in `test_gcp_contains.cpp` (temporary local edit) → rebuild target → run → expect exit-phase SIGSEGV/SIGABRT **after** `ALL assertions passed`; capture stderr + `dmesg`/gdb backtrace → paste below.
3. Revert the red edit.

| artifact | value |
|---|---|
| baseline run rc | pending |
| red run rc | pending |
| crash backtrace (top frames) | pending |
| root-cause verdict (A-n) | pending |

## 3. Retirement evidence (per WP-B round)
| file | before (site) | after | run 1 | run 2 | commit |
|---|---|---|---|---|---|
| (tracer) test_gcp_contains | :25 `_Exit` | listener retired | pending | pending | pending |

## 4. Fixture evidence (WP-C) — pending binaries
- 18 cases in 5 binaries registered (`ctest -N -R teardown` count to paste).
- Each binary uses shared ordered-teardown listener; no `_Exit` anywhere in new code (gate test enforces).

## 5. Exit-path evidence (WP-E) — pending binaries
- `test_exit_path_contracts_r4` cases: shared shutdown-pair drift check, shutdown idempotence, exitQgis no-instance idempotence.

## 6. Gate evidence (WP-G) — pending binaries
- `test_teardown_retirement_gate_r4`: parses RETIREMENT.md 退役 rows → asserts `_Exit`-free; manual drill (reintroduce one `_Exit` → gate red → revert) to be recorded below.
| drill | result |
|---|---|
| reintroduce `_Exit` into a retired file | pending |

## 7. Oracle run (final) — PENDING
`ctest -R "lifecycle|teardown|dialog_stress|view_link" -j1` in fresh build dir ×2 → logs + exit codes to paste; `git rev-list --count origin/master..HEAD`; deliverable floors table.
