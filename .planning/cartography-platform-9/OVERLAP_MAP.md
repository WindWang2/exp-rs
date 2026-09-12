# OVERLAP MAP — parallel-track conflict audit (2026-09-12)

Five sibling `-9` tracks are active. Audited against this track's owned
files via `git diff --name-only origin/master...origin/feat/<branch>`:

| Branch / PR | Owned-file overlap | Verdict |
|---|---|---|
| feat/scientific-algorithms-9 (#883) | none | clear |
| feat/model-runtime-multimodal-9 (#884) | none | clear |
| feat/spatial-scientist-harness-9 (#885) | `mapspec_conditions.cpp` listed, but the two-dot diff `origin/master..branch` for the file is **empty** — the branch's earlier `#866/#877/#867` fixes were superseded by master's `f316dfdbb4` through its own master merge (a819ac8298). Final content identical. | clear (shared files only: CHANGELOG.md, tests/CMakeLists.txt — append-only merges) |
| feat/scientific-mlops-9 (#886) | none | clear |
| feat/geospatial-data-fabric-9 (#887) | none (branched from the same base `f316dfdbb4`) | clear |

Shared files every track may touch (CHANGELOG.md, tests/CMakeLists.txt):
this track keeps its additions append-only and performs them once at the
final milestone to minimize merge friction, then re-syncs master before the
PR.

Remote `-5/-6/-7/-8` branches are merged residue (all contained in master
via the PR merges listed in BASELINE.md) — treated as history, not
development.

## Seam decisions with the Harness track

- Recipe degradation (#867) and anything in `src/agent/harness/**` belongs
  to spatial-scientist-harness-9. This track does not modify harness
  sources. The compose/preflight/repair/export/confirm closed loop is
  exposed through the existing cartography agent tools and the harness's
  typed map tools (`confirmMapOutput` etc.) — M10 only widens the typed
  surface inside owned files and keeps `data/agent/evals/**` untouched
  (harness-owned).
