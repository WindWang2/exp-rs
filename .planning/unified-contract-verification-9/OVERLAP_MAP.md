# OVERLAP_MAP — file-level conflict avoidance

Baseline `origin/master` = `f316dfdbb4`. Open `-9` PR footprints measured via
`gh pr view <n> --json files` on 2026-09-12.

| Path | Ours | Others | Policy |
|------|------|--------|--------|
| `src/contracts/` (new) | **OWN** | — | exclusive |
| `data/contracts/` (new) | **OWN** | — | exclusive |
| `tests/test_contract_*_9.cpp`, `tests/test_*_contract_9.cpp` (new) | **OWN** | — | exclusive |
| `docs/verification/` (new files only) | **OWN** | — | #884 touches docs/adr+models+inference, not docs/verification |
| `scripts/verification_ladder.py` | extend (additive) | — | single late commit |
| `scripts/collect_readiness.py` | extend (additive) | — | single late commit |
| `tests/CMakeLists.txt` | append test targets | all five PRs append tests | late minimal commit, rebase before PR |
| root `CMakeLists.txt` | append one `add_subdirectory(src/contracts)` | #883/#884/#887 possible | late minimal commit, rebase before PR |
| `CHANGELOG.md` | append one section | all five PRs | single late commit |
| `src/operators/**` | read-only (+ scanner reads sources at test time) | #883, #884 | no writes |
| `src/agent/harness/**`, `src/agent/contracts/**`, `data/agent/**` | read-only | #885 | no writes |
| `src/cli/**` | read-only | #886 | no writes |
| `src/geospatial/**` | read-only | #887 | no writes |
| `src/app/**`, `src/help/**` | read-only | (workbench/track 6/7 semantics) | no writes; guard-only |
| `src/processing/framework/algorithm_meta*` | read-only | #883 (contracts dir) | no writes |

**High-risk seam**: `tests/CMakeLists.txt` and root `CMakeLists.txt` — every
track appends. Mitigation: one combined commit at the very end, minimal
hunks, rebase on latest origin/master immediately before push.

**Knowledge-seam risk**: `data/agent/capabilities` is #885-owned; our
capability-drift guards read it at test time only and never rewrite entries
(enforcement lives in tests, content fixes belong to #885).
