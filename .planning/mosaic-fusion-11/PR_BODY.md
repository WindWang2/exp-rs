# PR BODY — F15: Large-Scale Mosaic, Fusion & Quality Composite 11.0

> Branch `zcode/mosaic-fusion-11` · Baseline `origin/master@a5b11b7f10`
> **Local evidence only; no online CI dependency.**

## What lands

Production quality mosaicking and fusion-quality reporting as an **additive**
layer on top of the existing `rs:mosaic` / pan-sharpening capabilities
(ADR 0163):

- **A — Scene/grid plan** (`mosaic_plan`): deterministic reference grid,
  union extent, per-scene placement (sub-pixel flagged), composite order and a
  pairwise overlap inventory. Mixed-CRS / rotated / pixel-size-mismatched sets
  are *diagnosed* (mixed-CRS footprints transformed into the plan CRS) and the
  operator fails closed with an actionable message — no silent misalignment,
  no duplicated reprojection authority.
- **B — Radiometric balancing** (`mosaic_balancing`): overlap-graph BFS from a
  reference scene; three-pass streaming robust gain/bias fit (least squares →
  residual-MAD trim → inlier refit); cumulative gain clamps and bias-sigma
  anomaly gates; `fail`/`drop` policy. Deliberately separate from absolute
  calibration (D13 domain, open PR #1008).
- **C — Seamline** (`mosaic_seamline`): cost = radiometric |Δ| + gradient
  disagreement + cloud penalty + edge-distance pull; DP min-cost path with
  deterministic tie-breaks; binned builder bounds seam memory to ≤512×512
  cells regardless of scene size.
- **D — Blending** (`mosaic_blend`): feather ramp (unit-sum weights, per-pixel
  NoData fallback → no cracks/double boundaries) plus a windowed Laplacian
  multiband kernel with halo clamp (exact on constant inputs).
- **E — Quality composite** (`mosaic_quality` + operator): per-scene
  cloud/quality/time/view scoring with renormalized weights; per-pixel
  dominant-source provenance band; per-input contribution stats.
- **F — Fusion quality** (`fusion_quality_report`): Q index, RASE, per-band
  mean/std distortion ratios, ERGAS/CC/RMSE/SSIM, a configurable
  spectral-distortion guard, a streaming accumulator and a fixed-schema JSON
  artifact (atomic writes). `rs:image_fusion` gains the `hpf` method and an
  optional `qualityReport` output.
- **G — Atomic streaming output**: `rs:quality_mosaic` writes a tiled
  512²-block DEFLATE GeoTIFF to `<output>.part.tif` and publishes by rename;
  failures clean up; optional AVERAGE overviews degrade to warnings; the JSON
  report is written atomically.
- **H — Corpus & scale**: deterministic synthetic scenes with known
  gain/offset/cloud truths; scale tests prove peak buffered memory tracks the
  sampling window (not the scene) and seam cells stay bounded at a 100k-tile
  logical extent; mid-stream failure surfaces clean errors;
  `EXP_MOSAIC_SCALE_E2E=1` opts into the heavy real-raster run.

## Dedupe / ownership vs concurrently open PRs

At branch time the only open PR was **#1008 (`zcode/radiometric-spectral-workbench`)**
— absolute calibration/6S/spectral. File-level overlap with this track: none in
business code; shared integration files only (`tests/CMakeLists.txt`,
`.gitignore` untouched by us). Its changed files were treated as read-only.
Details: `.planning/mosaic-fusion-11/PARALLEL_OWNERSHIP.md`.

## Compatibility

- `rs:mosaic` untouched (byte-compatible contract); GUI dialogs untouched.
- New operator `rs:quality_mosaic` registered through the existing seams
  (rs_operators_init, capability catalog, scientific contract).
- `processNativeFusion` gained an appended defaulted out-parameter; existing
  callers compile unchanged.
- GDAL API note: `OGRCoordinateTransformation::DestroyCT` (GDAL ≥ 3.9-style)
  used for footprint transforms.

## Verification (local, offline; -j2 build / -j1 tests, offscreen Qt)

| Suite | What it proves |
|---|---|
| `test_mosaic_plan` | hand-computed grid geometry, mixed-CRS envelope via PROJ truth, fail-closed negatives |
| `test_mosaic_balancing` | known gain/offset recovery (exact truth), 3-scene chaining, anomaly rejection, cloud-robust fit |
| `test_mosaic_seamline` | hand-computed DP answers, wall detour, tie-break determinism, binned corridor, cloud dominance, window-partition invariance |
| `test_mosaic_blend` | unit-sum weights, NoData fallback, exact constant reconstruction, weight-extreme exactness, halo clamp |
| `test_mosaic_quality` | hand-derived scores, renormalization, clamping, deterministic ordering |
| `test_fusion_quality_report` | unit indices on identical bands, hand-derived distortion ratios, degenerate-input violations, schema round-trip, atomic write |
| `test_quality_mosaic_operator` | GDAL-fixture E2E: balancing truth, provenance traceability, no-crack, tiled output, report schema, atomic failure, mask alignment guard |
| `test_mosaic_scale` | memory-vs-window bound via counting sampler, 100k-tile logical seam bound, mid-stream failure, opt-in heavy run |
| regression | `test_mosaic`, `test_pansharpening`, `test_image_fusion` (incl. new HPF known-answer + report verdict tests) |

Final gate (local, clean consistent rebuild): **83/83 tests passed, run twice
consecutively** (Oracle: identical results both runs). Exit codes and evidence
trail: `.planning/mosaic-fusion-11/{TEST_MATRIX,EVIDENCE,REVIEW_LOG}.md`.

`git diff --check origin/master...HEAD` clean; no conflict markers; secret
scan clean.

### Known blocked-by-master regressions (documented, out of scope)

`test_pansharpening` / `test_capability_drift` / `test_capability_knowledge`
/ `test_rs_operators` cannot be built in this worktree because
`origin/master@a5b11b7f10` (#1000) itself breaks the `sicnu_agent` build
(`data_platform_tools.cpp` references `BenchmarkService` without its
namespace; file is byte-identical to origin/master — proof in
`EVIDENCE.md` → OUT_OF_SCOPE). This track's registration consistency is
covered by the registry/catalog/contract edits plus the F15 suites.

## Known limitations / follow-ups

- Provenance band is Float32 (exact for ≤ 2²⁴ scenes).
- Operator blending exposes `none`/`feather`; multiband stays a kernel-level
  module (documented).
- The operator `qualityReport` on `rs:image_fusion` is the full-resolution
  *fidelity* form; the Wald degraded pass remains a kernel-level capability.
- GUI workbench integration for `rs:quality_mosaic` (follow-up; D-011).
- Cross-CRS mosaicking requires prior reprojection (by design; plan names the
  exact action).

## Review

- Round 1: main-agent full-diff review → 3 findings fixed (`e8b54fa7cd`).
- Round 2: independent read-only adversarial subagent over the complete
  diff → 14 findings, all dispositioned: **1 P0 fixed** (HPF pan-center
  sample), **3 P1** (2 fixed with regressions, 1 rejected with evidence —
  reviewer arithmetic), **5 P2** fixed/mitigated+documented, **5 P3** fixed.
  Final: **P0 = 0, P1 = 0**. Full log:
  `.planning/mosaic-fusion-11/REVIEW_LOG.md`.
