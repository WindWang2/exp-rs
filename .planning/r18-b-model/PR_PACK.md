# hardening(r18-b): 既有遥感 AI 模型推理 — 恢复被回滚的 r15-C + feather/发布/分块修复

Generated: 2026-10-19T02:56:56Z

## Baseline

- base `origin/master` @ `a60c8831c9cc4e64701d17bde2ace67a74a9b7a0` (Merge pull request #1513 from WindWang2/hardening/r15-B-geodata-persistence)
- head `HEAD` @ `7efd33264a4494575890ae2fdd42433f5f8fc25b` (chore(r18-b): drop the reference patches' EOF blank line)
- commits in range: 10

## Scope

- sources (15): `src/operators/framework/chunked_run.cpp`, `src/operators/framework/model_catalog.cpp`, `src/operators/runtime/detection_postprocess.cpp`, `src/operators/runtime/device_planner.cpp`, `src/operators/runtime/model_ensemble.cpp`, `src/operators/runtime/model_execution_service.cpp`, `src/operators/runtime/model_publish.cpp`, `src/operators/runtime/model_runtime.cpp`, `src/operators/runtime/opencv_dnn_runtime.cpp`, `src/operators/runtime/openvino_provider.cpp`, `src/operators/runtime/python_worker_provider.cpp`, `src/operators/runtime/tensorrt_provider.cpp` …
- public_headers (4): `src/operators/runtime/model_runtime.h`, `src/runtime/chunk/chunk_graph.h`, `src/runtime/chunk/chunk_pipeline.h`, `src/runtime/chunk/memory_planner.h`
- tests (19): `tests/CMakeLists.txt`, `tests/data/gen_test_infer_addmul.py`, `tests/data/py_worker_slow_start.py`, `tests/data/test_infer_addmul.onnx`, `tests/test_chunk_graph.cpp`, `tests/test_eo_platform_10.cpp`, `tests/test_model_ensemble.cpp`, `tests/test_model_runtime_9.cpp`, `tests/test_model_runtime_bench.cpp`, `tests/test_model_tasks.cpp`, `tests/test_multimodal_inference.cpp`, `tests/test_provider_python.cpp` …
- docs (26): `.planning/r18-b-model/BASELINE.md`, `.planning/r18-b-model/CROSS_TRACK_FINDINGS.md`, `.planning/r18-b-model/DECISIONS.md`, `.planning/r18-b-model/FINAL_REPORT.md`, `.planning/r18-b-model/KNOWN_FINDINGS.md`, `.planning/r18-b-model/LOOP_LEDGER.md`, `.planning/r18-b-model/ORACLES.md`, `.planning/r18-b-model/OWNERSHIP.md`, `.planning/r18-b-model/PERFORMANCE.md`, `.planning/r18-b-model/PR_BODY.md`, `.planning/r18-b-model/PR_PACK.md`, `.planning/r18-b-model/REVIEW_R1.md` …
- other (3): `.gitignore`, `.planning/r18-b-model/references/w-fixes-v2.patch`, `.planning/r18-b-model/references/w-fixes.patch`

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

overlap evidence for this diff's 67 paths:
- open_prs: 0
- merged_prs: 11
  - `hardening/r16-d-workbench-cartography` touches 1 of these paths
  - `hardening/r16-e-help-delivery` touches 1 of these paths
  - `hardening/r16-a-evidence-verification` touches 1 of these paths
  - `hardening/r16-c-teaching-classroom` touches 2 of these paths
  - `hardening/r16-b-scientific-decisions` touches 2 of these paths
  - `hardening/r15-E-desktop-teaching-help` touches 2 of these paths
  - `hardening/r15-D-pi-mcp-workflow` touches 30 of these paths
  - `hardening/r15-C-model-runtime` touches 30 of these paths
  - `hardening/r15-A-science-processing` touches 1 of these paths
  - `hardening/r15-B-geodata-persistence` touches 1 of these paths
  - `hardening/r14-D-qt-qgis-cartography` touches 1 of these paths
- remote_branches: 1
  - `origin/hardening/r10-algorithm-processing-quality` touches 1 of these paths
- sibling_worktrees: 1
  - `refs/heads/agent/r18-e-teaching-evidence` touches 1 of these paths

---

## Diff stat

```
.gitignore                                         |   9 +
 .planning/r18-b-model/BASELINE.md                  |  39 +++
 .planning/r18-b-model/CROSS_TRACK_FINDINGS.md      |  20 ++
 .planning/r18-b-model/DECISIONS.md                 |  12 +
 .planning/r18-b-model/FINAL_REPORT.md              |  48 +++
 .planning/r18-b-model/KNOWN_FINDINGS.md            |  32 ++
 .planning/r18-b-model/LOOP_LEDGER.md               |   9 +
 .planning/r18-b-model/ORACLES.md                   |  16 +
 .planning/r18-b-model/OWNERSHIP.md                 |  42 +++
 .planning/r18-b-model/PERFORMANCE.md               |  39 +++
 .planning/r18-b-model/PR_BODY.md                   |  43 +++
 .planning/r18-b-model/PR_PACK.md                   | 154 ++++++++
 .planning/r18-b-model/REVIEW_R1.md                 |  21 ++
 .planning/r18-b-model/REVIEW_R2.md                 |  34 ++
 .planning/r18-b-model/TASKS.md                     |  17 +
 .planning/r18-b-model/TEST_RESULTS.md              | 130 +++++++
 .planning/r18-b-model/references/r15-c-BASELINE.md |  53 +++
 .../r18-b-model/references/r15-c-FINAL_REPORT.md   | 107 ++++++
 .../references/r15-c-ISSUE_PR_REVIEW_MATRIX.md     |  48 +++
 .planning/r18-b-model/references/r15-c-LEDGER.md   | 105 ++++++
 .planning/r18-b-model/references/r15-c-ORACLES.md  |  29 ++
 .../r18-b-model/references/r15-c-OWNERSHIP.md      |  43 +++
 .../r18-b-model/references/r15-c-PERFORMANCE.md    |  36 ++
 .planning/r18-b-model/references/r15-c-PR_BODY.md  |  54 +++
 .../r18-b-model/references/r15-c-REVIEW_ROUND1.md  |  41 +++
 .../r18-b-model/references/r15-c-REVIEW_ROUND2.md  |  30 ++
 .../r18-b-model/references/r15-c-TEST_RESULTS.md   |  90 +++++
 .planning/r18-b-model/references/w-fixes-v2.patch  | 388 +++++++++++++++++++++
 .planning/r18-b-model/references/w-fixes.patch     | 345 ++++++++++++++++++
 src/operators/framework/chunked_run.cpp            |  43 ++-
 src/operators/framework/model_catalog.cpp          |  58 +++
 src/operators/runtime/detection_postprocess.cpp    |  17 +-
 src/operators/runtime/device_planner.cpp           |  31 +-
 src/operators/runtime/model_ensemble.cpp           |  18 +-
 src/operators/runtime/model_execution_service.cpp  |  51 ++-
 src/operators/runtime/model_publish.cpp            | 117 ++++++-
 src/operators/runtime/model_runtime.cpp            |  83 ++++-
 src/operators/runtime/model_runtime.h              |   9 +
 src/operators/runtime/opencv_dnn_runtime.cpp       |  13 +-
 src/operators/runtime/openvino_provider.cpp        |  86 ++++-
 src/operators/runtime/python_worker_provider.cpp   |  30 +-
 src/operators/runtime/tensorrt_provider.cpp        |  41 ++-
 src/operators/runtime/tile_inference_engine.cpp    | 188 ++++++++--
 src/runtime/chunk/chunk_graph.cpp                  |  56 ++-
 src/runtime/chunk/chunk_graph.h                    |   9 +
 src/runtime/chunk/chunk_pipeline.cpp               |  22 +-
 src/runtime/chunk/chunk_pipeline.h                 |   5 +
 src/runtime/chunk/memory_planner.h                 |  10 +
 tests/CMakeLists.txt                               |  26 ++
 tests/data/gen_test_infer_addmul.py                | 113 ++++++
 tests/data/py_worker_slow_start.py                 |  67 ++++
 tests/data/test_infer_addmul.onnx                  | Bin 0 -> 257 bytes
 tests/test_chunk_graph.cpp                         |  67 ++++
 tests/test_eo_platform_10.cpp                      | 115 +++++-
 tests/test_model_ensemble.cpp                      |  71 ++++
 tests/test_model_runtime_9.cpp                     |  90 +++++
 tests/test_model_runtime_bench.cpp                 |   3 +
 tests/test_model_tasks.cpp                         |  75 ++++
 tests/test_multimodal_inference.cpp                |  66 ++++
 tests/test_provider_python.cpp                     |  75 ++++
 tests/test_r14_B_runtime_providers.cpp             |  75 ++++
 tests/test_r15_C_chunk_admission.cpp               | 226 ++++++++++++
 tests/test_r15_C_detection_decode.cpp              | 114 ++++++
 tests/test_r15_C_manifest_contracts.cpp            | 203 +++++++++++
 tests/test_r15_C_provider_degradation.cpp          | 257 ++++++++++++++
 tests/test_r15_C_publish_recovery.cpp              | 196 +++++++++++
 tests/test_r15_C_runtime_contracts.cpp             | 162 +++++++++
 67 files changed, 4831 insertions(+), 91 deletions(-)
```

## Commits

```
7efd33264 chore(r18-b): drop the reference patches' EOF blank line
f9885f3fa docs(r18-b): review pack
1b535e5c1 chore(r18-b): strip trailing whitespace from the reference patches (diff --check clean)
0557dc22f docs(r18-b): final report
0592b1913 fix(review-r2): apply the three R2 P3s — real span correction, tracked r15-C references, write-side window guard
e0d334384 docs(review): record R1 verdict (PASS-WITH-NITS, 8 P3) and dispositions
498012d25 harden(runtime): clamp feather flush to the window bottom (R1 review)
59aa6e420 test(runtime): R18-B regression tests + fixtures + track evidence
e6e91f71e fix(runtime): feather slide corruption, deferred-NoData marks, publish fsync parity
a6ac18bcd restore(r15-c): reinstate the entire model-runtime track reverted by the #1516 merge
```
