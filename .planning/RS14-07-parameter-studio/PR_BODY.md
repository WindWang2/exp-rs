# RS14-07 completion: exemplars + e2e + bridge build wiring + docs

## 目标 / Goal

PR #1198 merged the RS14-07 Parameter Sensitivity & Uncertainty Studio core
(`src/study`, slices A–G of the plan) — but four Definition-of-Done items
from `.planning/RS14-07-parameter-studio/plan.md` did not survive the merge:

1. `tests/test_study_e2e.cpp` was in the tree with **no build target**, and
   referenced `examples/studies/ndvi-threshold.sicnu-study.json` — a file
   that does not exist anywhere in master history (verified with
   `git log --all --diff-filter=A -- "*sicnu-study*"`). The "full-stack
   teaching e2e, 91 assertions green" claimed in the #1198 description was
   never a buildable, runnable artifact on master.
2. The **exemplar specs** (track deliverable: NDVI threshold /
   classification parameter / change threshold) shipped zero files.
3. `src/study/bridge` (`Sicnu::study_bridge`, the production
   ExecutionPlane adapter) had its own CMakeLists but **no
   `add_subdirectory` anywhere** — dead code.
4. The docs deliverables (parameter studio guide; integration wiring
   points in `docs/integration.md`) were absent; `docs/integration.md` had
   no study section.

This branch closes exactly those gaps. No previously merged behavior is
changed; the study library sources are untouched.

## 架构 / What lands

| Slice | Files | Content |
|---|---|---|
| W1 | `examples/studies/*.sicnu-study.json` (×3), `tests/test_study_exemplars.cpp` | Three teaching exemplars — `ndvi-threshold` (OAT, `rs:threshold_raster.threshold`, 9 runs, spatial on, **no** objective metric), `classification-training-budget` (grid, `rs:supervised_classification.maxSamplesPerClass`, 5 sets × 3 seed replicates, held-out `overallAccuracy`/`kappa` via `testSplit: 0.3`, the one spec that declares an objective metric), `change-threshold` (seeded LHS, `rs:change_detection.threshold`, 6 × 2 runs) — plus a light contract suite that loads every shipped spec through the **production reader** (strict version + unknown-field refusal), samples each to its declared shape and replays it deterministically. Shipped teaching templates cannot silently rot. |
| W2 | root `CMakeLists.txt`, `src/study/bridge/CMakeLists.txt` | `add_subdirectory(src/study/bridge)` directly after `src/processing` (it links `sicnu_task_center`, defined there; pattern: `src/experiment/bridge` after its dependency). jsoncpp linked PRIVATE in the bridge body following the `sicnu_processing` canonical-target resolution. |
| W3 | `tests/CMakeLists.txt` | `sicnu_add_test(test_study_e2e)` + `Sicnu::study` + `Sicnu::study_bridge` + `GDAL::GDAL` — the NDVI exemplar sweeps the real spine (ExecutionPlane → TaskCenter → JobEngine → `rs:threshold_raster`). |
| W4 | `docs/experiments/parameter-studio.md`, `docs/integration.md`, `.planning/.../progress.md` | Teaching + agent entry-point doc; RS14-07 integration seams (agent `study:*` tools lane, workbench panel over `StudyRunRow`, ExecutionPlane consumers, capsule/lab embedding) — documented wiring points only, none built. |

## 测试证据 / Test evidence

- Light lane (offline, `-j1`, no QGIS): `test_study_spec` 62,
  `test_study_sampling` 161, `test_study_runner` 125,
  `test_study_analysis` 149, `test_study_spatial` 69, `test_study_export`
  99, `test_study_exemplars` 168 — all green (regression: the merged #1198
  suites stay green on this branch).
- Full stack: `test_study_e2e` — **91 assertions green** (single cold heavy
  build, then executed with `QT_QPA_PLATFORM=offscreen`): the shipped NDVI
  exemplar spec loads through the production reader, sweeps a synthetic
  16×16 NDVI raster through `rs:threshold_raster` on the real spine, every
  point gets a committed raster + recorded `maskedPercent`, spatial
  difference summaries vs the baseline run, and the exported
  `sicnu.studyreport.v1` carries both curves (decreasing `maskedPercent`,
  "fall" narrative), empty `declared_best`/Pareto, and standalone-parseable
  agent evidence.
- Build discipline: one cold configure + a single heavy-chain build
  (`-j2`, machine idle ≥30 GB free; light work at `-j1`), per the
  campaign resource rules.

## Review / Review gate

Two independent adversarial review rounds; full record in
`.planning/RS14-07-parameter-studio/progress.md`.

**Round 1** (over `a9dc33fa7..HEAD` + docs): 1×P1 — the classification
exemplar's declared metrics were unreachable (`overallAccuracy`/`kappa`
need `testSplit > 0`, default 0; `meanConfidence` needs probabilityOutput,
which `svm` rejects) — fixed with `testSplit: 0.3` + the held-out pair;
6×P3 (doc wording vs code: nonexistent `sicnu.studyspec.v1` marker,
overstated production wiring, `spec_json` key name; `<QSet>` include;
contains-only metric oracle → exact assertions; LHS different-seed oracle
now compares sampled values instead of seed-embedding pointIds). Fixed in
`45aafffc2`.

**Round 2** (fix verification + fresh eyes; reviewer replicated the
sampler RNG externally): fixes verified; e2e compile fixes confirmed
(`sicnu::dataset::runStatusToString` namespace drift; Catch2 WARN needs
`.toStdString()`). Three residuals fixed: the LHS case had no exact
metrics assertion (and the round-1 record falsely claimed it did —
corrected); the classification exemplar's swept parameter was INERT
(`rejectThreshold` only bites behind `uncertaintyOutput`, which `svm`
rejects — all-ties metric field) → exemplar now
`classification-training-budget.sicnu-study.json` sweeping
`maxSamplesPerClass`; the "uncertainty across seeds" doc claim reworded
(replicate seeds are recorded but not injected into submissions —
zero-width bands are the truthful null). Verdict: **PR-ready**.

## 已知限制 / Known limitations

- The classification exemplar sweeps `maxSamplesPerClass` (training budget)
  rather than `rejectThreshold` — the latter only bites behind
  `uncertaintyOutput`, which `svm` rejects, so a rejectThreshold sweep would
  produce an all-ties metric field (round-2 review finding N1).
- Replicate seeds are RECORDED per run but not injected into submitted
  parameters (the execution spine has no seed field): deterministic
  operators that ignore the recorded seed yield honest zero-width envelope
  bands. Documented in the guide; changing the runner's submission contract
  is deliberate non-scope for this completion branch.
- The spec document carries `schema_version: 1` only — there is no
  `document_type` literal for specs (the report has
  `sicnu.studyreport.v1`); docs now say exactly that.
- The study panel (GUI) and `study:*` agent tools remain DESIGNED wiring
  points, documented in `docs/integration.md` — deliberately not built
  here to avoid cross-track collisions.

## 与 open issues 的去重 / Dynamic dedup

Re-checked at PR time: none of the open issues/PRs duplicate this work; the
avoid-list issues (#1146–#1187 per recon §4) are untouched — no TaskCenter,
Workflow, operator-algorithm, plugin, or catalog code changed. Open PRs
#1237–#1243 (lab cockpit, teaching admin, experiment studio UI, science
context broker, agent ops, geospatial/spectral hardening) operate on other
surfaces; #1238's experiment-studio UI is a GUI layer over different
stores — no file overlap with this branch.

## 与其他 19 tracks 的边界 / Track boundaries

- Single source of truth: runs live in `ExperimentStore`, point identity is
  the matrix `cellId`, Pareto reuses `MatrixAggregator` — no second
  registry/store/provenance.
- The runner spawns no thread pool; the in-flight window is a submission
  bound over the existing ExecutionPlane spine (admission stays with
  TaskCenter).
- Agent/UI integration points are documented, not implemented.
