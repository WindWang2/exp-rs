# F13 · Multimodal Registration & Geometric Accuracy 11.0

**Baseline:** `origin/master @ a5b11b7f10` (branch `zcode/multimodal-registration-11`, worktree `../exp-rs-multimodal-registration-11`)
**Local evidence only; no online CI dependency.**

## P0 (out of scope, minimal unblocking fixes — pre-existing master breakage)

Two compile breaks on master blocked the shared build tree for every track; both are fixed with one line each and verified by standalone syntax checks (details in `.planning/multimodal-registration-11/EVIDENCE.md`):

1. `src/app/workbench/mission_context_store.cpp` — missing `#include <QDir>` (introduced by D18; the TU has never compiled on this host). Fixes the whole-tree `all` build.
2. `src/agent/data_platform_tools.cpp` — unqualified `BenchmarkService` (introduced by D19; `using sicnu::experiment::BenchmarkService;` added). `sicnu_agent` could not compile without it.

Also pre-existing and NOT touched: `test_capability_drift` is red on master (duplicate ids in preprocess.json; uncovered io:/cartography:/recipe entries; unrelated to this diff — this PR only ADDS coverage for its own new surfaces via `data/agent/capabilities/geometric.json`), and `test_mission_context`/`test_mission_e2e_scaffolding` link failures (target source list missing `workbench_host.cpp`).

## Dedupe / parallel ownership (startup audit)

- Startup facts refreshed: `origin/master @ a5b11b7f10`; PR #991/#992 already merged; only open PR was #1008 (spectral, `zcode/radiometric-spectral-workbench`) — zero business-file overlap; shared integration files touched append-only.
- Open issues #1001–#1007 deduped: all out of scope except **#1005 (georef, P1) which this PR fixes** (fail-closed GCP picks). The issue is not closed from here.
- All changed files fall inside the track's primary write scope; `src/processing/algorithms/sar/*` and D18 workbench files untouched.

## What this delivers

New module `src/processing/algorithms/registration/` (`sicnu::registration`), consuming — not rewriting — the D14 seams (GeometricTransform, TpsInterpolator, FeatureMatcher RANSAC, Resampler, GcpManager):

- **Cross-modal matching** (`MultimodalMatcher`): whitened FFT phase correlation (deterministic in-repo FFT, no new dependencies) + normalized mutual information / NCC metrics (`Auto` = phase down the pyramid, MI at the finest level), NaN-aware [1,2,1]/4 pyramid with per-cell offset propagation, per-window metric fallback, RANSAC consensus, full-extent coverage gating (spatially clustered matches cannot rate high — the Oracle), and explicit refusal semantics (`too_few_matches`, `flat_region`, `low_peak_snr`, `insufficient_coverage`, `low_consensus`, `cancelled`, `cap_exhausted`).
- **Model selection** (`ModelSelector`): Translation→Similarity→Affine→Projective→Poly2→Poly3→TPS ladder adopted only on >=10% k-fold held-out RMSE improvement with the kappa gate; full per-candidate evidence table returned (nothing hidden).
- **RPC bias refinement** (`RpcBiasModel`): robust median constant vs 6-parameter affine ground bias promoted on held-out evidence (>=6 samples), height-sensitivity statistics. GDAL RPC coefficients are never modified (ADR 0057 preserved).
- **Stack registration** (`StackRegistrator`): global 2-DoF translation least squares over a weighted pair graph (<=256 scenes), reference suggestion/pinning, disconnected-scene reporting, loop-closure drift via post-adjustment edge residuals.
- **Quality products** (`RegistrationQuality`): empirical CE90 (nearest-rank, degraded flag below 20 samples, Rayleigh 2.146-sigma reference), residual vector field, per-point local confidence capped by coverage, `exp_rs_registration_quality/1` atomic JSON sidecar (QSaveFile).
- **Operators**: `rs:register_images` (match -> affine fit -> reverse-mapped warp in decimated-buffer pixel space -> GeoTIFF with decimation-aware reference geotransform + optional quality sidecar; refusal throws and writes nothing), `rs:stack_register` (adjustment + `exp_rs_stack_registration/1` sidecar).
- **Agent surface**: the D14 `spatial:geometric_registration` tool was compiled but never registered — fixed via `GeometricSpatialTool`; three new actions `multimodal_register` / `select_model` / `stack_register` return structured explain evidence.
- **#1005**: `mapPickToLayerCrs` delegates to `rsGeorefTransformPickBetweenCrs` returning `std::optional<QgsPointXY>` — a throwing transform or invalid canvas CRS refuses the GCP pick (fail-closed), while the unreferenced-raster (invalid layer CRS) pixel-space workflow is preserved.
- **Docs**: ADR 0160, `docs/processing/geometric-registration.md`, CHANGELOG entry.

## Architecture decisions (DECISIONS.md D-001..D-015, highlights)

New `registration/` subdir (scope `*registration*`, `sicnu::<dir>` namespace precedent); in-repo FFT instead of a new dependency; MI via 16-bin robust quantization; CV+ladder selection instead of AIC (residuals are spatially correlated); constant-first RPC bias with opt-in-style affine promotion; translation-only global adjustment (bundle adjustment explicitly not-supported); empirical CE90; coverage normalized over the full source extent.

## Compatibility

- Additive only: no D14 API/behavior changes; `QgsRpcGcpTransformer`/`setRpcOptions`/`qgsleastsquares` untouched; existing georeferencer UI behavior preserved except the #1005 fail-closed pick (which also preserves the unreferenced-raster pass-through).
- New operators follow the three-point registry pattern (macro + explicit `add()` + CMake) and are headless-runnable via pipeline JSON.
- No new third-party dependencies; processing-layer code stays Qt-core/QString only; deterministic (fixed iteration order, seeded RANSAC).

## Local tests (all commands `QT_QPA_PLATFORM=offscreen`, serial)

Double-pass Phase 8 validation after rebase — **20/20 suites, exit 0, both passes**:
new suites `test_registration_fft`, `test_multimodal_matcher`, `test_model_selector`, `test_rpc_bias_model`, `test_stack_registrator`, `test_registration_quality`, `test_registration_operators`, `test_registration_e2e`, `test_georef_crs_pick_failclosed`, `test_geometric_agent_tools`;
regression `test_gcp_manager`, `test_geometric_transform`, `test_tps_interpolator`, `test_feature_matcher`, `test_resampler`, `test_pansharpening`, `test_d14_geometric_registration_e2e`, `test_rpc_gcp_refine`, `test_rpc_transformer`, `test_rpc_golden`.
All oracles are hand-derived/analytic (documented shifts, homographies, percentiles, coefficient recovery); production code never validates itself.

## Resource evidence

Host 16 cores / 64 GiB (3+ concurrent track builds during this work): builds at `-j2` (load stayed < the 24 = 1.5x-cores threshold), tests serial. Matcher scratch is a pinned closed form (~4.2 MiB at 512^2/window 64) gated by `maxWorkingMiB` (512 MiB default); operator `maxDim` clamped 64..4096; stack solver capped at 256 scenes (~2 MiB dense system). Sidecars write atomically via QSaveFile.

## Review

Subagent #2 adversarial review of the full diff found P0=1, P1=2, P2=2, P3=8 — all 13 dispositioned (12 fixed, 1 partial with docs), including a P0 where the operator warp mixed pixel- and world-space frames (content-level test added; it also surfaced a WarpOptions default-clamp product defect). Details: `.planning/multimodal-registration-11/REVIEW_LOG.md`.

## Known limitations / follow-ups

- Global stack adjustment is translation-only; per-pair affine stays in `rs:register_images`; no bundle adjustment.
- `RpcBiasModel` is the processing-layer authority; wiring it into `qgsrpcgcptransformer` (replacing its constant-median step) is a follow-up — the transformer's current behavior is unchanged and test-pinned.
- CE90 below 20 inliers is flagged degraded (worst observed error) rather than fabricated.
- Automated pair-graph construction across a scene set (currently the caller supplies pairwise translations from `rs:register_images` outputs).

## Hygiene

`git diff --check origin/master...HEAD` clean; no conflict markers; no secrets; working tree clean after commit.
