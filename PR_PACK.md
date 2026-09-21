# feat(models): EO model runtime 13.0 — detection/scene ensembles, bounded parallel members, compressed staging, derived outputs

Generated: 2026-09-20T21:26:05Z

## Baseline

- base `origin/master` @ `79adfe78a16b9419eef180cf9e6e5739658621a2` (Merge pull request #1134 from WindWang2/agent/flash-plugin-sdk-12)
- head `HEAD` @ `c9d0cd6c9a6a6deb25817a2586f856621d471da1` (fix(models): review remediation for ensemble 13.0 (P1 publish/rollback + provenance truth, P2 hardening))
- commits in range: 7

## Scope

- sources (6): `src/operators/framework/model_catalog.cpp`, `src/operators/runtime/detection_fusion.cpp`, `src/operators/runtime/detection_postprocess.cpp`, `src/operators/runtime/detection_tile_engine.cpp`, `src/operators/runtime/model_ensemble.cpp`, `src/operators/runtime/tile_inference_engine.cpp`
- public_headers (5): `src/operators/framework/model_catalog.h`, `src/operators/runtime/detection_fusion.h`, `src/operators/runtime/detection_tile_engine.h`, `src/operators/runtime/model_ensemble.h`, `src/operators/runtime/tile_inference_engine.h`
- tests (6): `tests/CMakeLists.txt`, `tests/test_detection_nms_10.cpp`, `tests/test_ensemble_detection.cpp`, `tests/test_ensemble_parallel.cpp`, `tests/test_ensemble_scene.cpp`, `tests/test_model_ensemble.cpp`
- docs (2): `docs/adr/0171-model-runtime-13-ensemble-execution.md`, `src/operators/CMakeLists.txt`

## Non-goals

<!-- TODO: what this PR deliberately does not do -->

## Design

<!-- TODO: design notes and alternatives rejected -->

## Issue mapping

<!-- TODO: issue numbers this PR closes or relates to -->

## Tests

<!-- TODO: exact commands and their output, run twice -->

## Resources

<!-- TODO: build parallelism used (-j1/-j2), targeted vs full runs -->

## Review disposition

<!-- TODO: reviewer findings and their fix state -->

## Known limitations

<!-- TODO: deferred P2/P3 with reproducible evidence, or 'none' -->

## Conflict hotspots

overlap evidence for this diff's 19 paths:
- open_prs: 4
  - `track/ds41-capability-search-13` touches 1 of these paths
  - `agent/glm53-mission-runtime-13` touches 1 of these paths
  - `agent/ds41-offline-labs-13` touches 1 of these paths
  - `agent/flash-temporal-phenology-12` touches 2 of these paths
- merged_prs: 9
  - `agent/flash-taskcenter-runtime-12` touches 1 of these paths
  - `agent/ds41-build-portability` touches 1 of these paths
  - `agent/ds41-performance-observatory` touches 1 of these paths
  - `agent/ds41-fuzz-boundaries` touches 1 of these paths
  - `agent/glm53-mission-workbench-12` touches 1 of these paths
  - `agent/flash-hyperspectral-12` touches 7 of these paths
  - `agent/flash-model-runtime-12` touches 7 of these paths
  - `agent/flash-data-experiment-12` touches 1 of these paths
  - `agent/flash-geospatial-fabric-12` touches 1 of these paths
- remote_branches: 11
  - `origin/agent/ds41-http-fetch-strict` touches 1 of these paths
  - `origin/agent/ds41-offline-labs-13` touches 1 of these paths
  - `origin/agent/flash-geo-fabric-integrity` touches 1 of these paths
  - `origin/agent/flash-processing-atomic-errors` touches 1 of these paths
  - `origin/agent/flash-temporal-phenology-12` touches 2 of these paths
  - `origin/agent/flash-workflow-integrity` touches 1 of these paths
  - `origin/agent/glm53-desktop-lifecycle` touches 1 of these paths
  - `origin/agent/glm53-mission-runtime-13` touches 1 of these paths
  - `origin/agent/glm53-plugin-sdk-trust` touches 1 of these paths
  - `origin/fix/review-issues-1033-1056` touches 4 of these paths
  - `origin/track/ds41-capability-search-13` touches 1 of these paths
- sibling_worktrees: 1
  - `refs/heads/agent/flash-sar-radiometry-13` touches 1 of these paths

---

## Diff stat

```
.../0171-model-runtime-13-ensemble-execution.md    |  164 +++
 src/operators/CMakeLists.txt                       |    1 +
 src/operators/framework/model_catalog.cpp          |  115 +-
 src/operators/framework/model_catalog.h            |   54 +-
 src/operators/runtime/detection_fusion.cpp         |  231 ++++
 src/operators/runtime/detection_fusion.h           |   79 ++
 src/operators/runtime/detection_postprocess.cpp    |   24 +
 src/operators/runtime/detection_tile_engine.cpp    |  372 ++++---
 src/operators/runtime/detection_tile_engine.h      |   25 +-
 src/operators/runtime/model_ensemble.cpp           | 1115 +++++++++++++++++---
 src/operators/runtime/model_ensemble.h             |   57 +-
 src/operators/runtime/tile_inference_engine.cpp    |  147 ++-
 src/operators/runtime/tile_inference_engine.h      |   41 +
 tests/CMakeLists.txt                               |   56 +
 tests/test_detection_nms_10.cpp                    |   24 +-
 tests/test_ensemble_detection.cpp                  |  734 +++++++++++++
 tests/test_ensemble_parallel.cpp                   |  758 +++++++++++++
 tests/test_ensemble_scene.cpp                      |  837 +++++++++++++++
 tests/test_model_ensemble.cpp                      |   55 +-
 19 files changed, 4471 insertions(+), 418 deletions(-)
```

## Commits

```
c9d0cd6c9 fix(models): review remediation for ensemble 13.0 (P1 publish/rollback + provenance truth, P2 hardening)
355addd2a docs(models): ADR 0171 — ensemble execution semantics (WBF, scene, concurrency, staging, derived outputs)
8aee9bff7 test(models): ensemble 13.0 oracles — WBF, scene, parallel admission, staging bytes
1a1af6b9a feat(models): EO model runtime 13.0 — detection/scene ensembles, bounded parallel members, compressed staging, derived outputs
68b40abad feat(models): ensemble contract 13.0 — wbf combination, fusion knobs, member budget, staging compression
fd7353664 refactor(runtime): scene classification core extraction + shared artifact publish
a23eaa5c8 feat(runtime): detection fusion core — canonical WBF module + engine seam
```
