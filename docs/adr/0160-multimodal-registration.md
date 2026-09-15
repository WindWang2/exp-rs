# ADR 0160 — Multimodal Registration & Geometric Accuracy (F13)

Status: accepted (track `zcode/multimodal-registration-11`)
Date: 2026-09-15
Builds on: ADR 0159 (geometric registration workbench), ADR 0057 (RPC GCP transformer)

## Context

D14 delivered single-modality feature matching (SIFT-like/ORB-like descriptors + deterministic
RANSAC), closed-form transform solving, TPS, resampling, GCP analytics, and the dual-window
workbench. Still missing after an evidence audit of master `a5b11b7f10` (grep: zero hits):

- phase correlation, mutual information, image pyramids / coarse-to-fine matching;
- evidence-driven transform model selection (only a static decision tree existed);
- RPC bias refinement beyond a constant lon/lat median translation;
- multi-scene stack registration (pair graph, global adjustment, loop-closure drift);
- CE90 / residual vector fields / local confidence products;
- a headless (`rs:*`) registration surface — the D14 agent tool was compiled but never
  registered into `SpatialToolRegistry`, so it was invisible to the tool catalog.

## Decisions

1. **New module** `src/processing/algorithms/registration/` (namespace `sicnu::registration`),
   consuming the D14 seams (`GeometricTransform`, `TpsInterpolator`, `FeatureMatcher::
   estimateHomographyRansac`, `Resampler`, `GcpManager`) instead of rewriting them.
2. **Deterministic FFT phase correlation** (power-of-two radix-2, no third-party FFT): windowed,
   Hann-windowed, peak-SNR trust score, wrapped parabolic sub-pixel refinement.
3. **Metrics as a seam**: `PhaseCorrelation` / `MutualInformation` (16-bin robust quantization,
   normalized by min marginal entropy) / `NormalizedCrossCorrelation`; `Auto` = phase
   correlation down the pyramid, MI at the finest level. Honest naming: no SIFT-equivalence
   claims for cross-modal work.
4. **Coarse-to-fine**: [1,2,1]/4 NaN-aware pyramid, per-level robust median offset, phase seeds
   every coarse level, metric-scan fallback per window when the phase peak fails (cross-modal
   windows can defeat the phase peak while retaining rank structure).
5. **Refusal semantics are part of the contract**: `RegistrationStatus {Success,
   LowConfidence, Refused, Failed}` with stable snake_case reason codes. Refusals write no
   output. Clustering cannot inflate quality: coverage is measured over the **full source
   extent**, and local confidence is capped by that coverage factor.
6. **Model selection** (Package C): fixed-complexity ladder Translation→Similarity→Affine→
   Projective→Poly2→Poly3→TPS; step up only on ≥10% relative **held-out** (k-fold, index-mod
   folds) RMSE improvement plus the existing κ gate. Evidence table reported, never hidden.
7. **RPC bias** (Package D): constant (robust median — preserves the D14 behavior) vs 6-parameter
   affine ground-space bias field, promoted only on held-out improvement with ≥6 samples; height
   sensitivity (dGround/dHeight via caller-supplied reprojection) feeds the quality report.
   GDAL RPC coefficients themselves stay untouched (ADR 0057 semantics preserved).
8. **Stack registration** (Package E): global 2-DoF translation adjustment over a weighted pair
   graph (reference pinned; dense normal equations, ≤256 scenes), BFS connectivity with
   disconnected scenes reported, post-adjustment edge residuals as loop-closure drift metrics.
   Global affine/bundle adjustment is explicitly not-supported.
9. **Quality products** (Package F): empirical CE90 (Rayleigh 2.146·σ reported as reference,
   flagged degraded below 20 samples), residual vector field over the source extent, per-point
   local confidence (score × residual × support × coverage), schema `exp_rs_registration_quality/1`
   written atomically via QSaveFile.
10. **Surfaces** (Package G): `rs:register_images` (match→fit→warp→report; refusal = error, no
    output), `rs:stack_register` (pure adjustment + sidecar), the D14 agent tool finally
    registered through the new `GeometricSpatialTool` adapter with three new actions
    (`multimodal_register`, `select_model`, `stack_register`).
11. **#1005 fail-closed**: `QgsGeorefShellWindow::mapPickToLayerCrs` returns
    `std::optional<QgsPointXY>`; invalid canvas/CRS/transform paths yield nullopt and the GCP
    pick is refused with a status-bar message instead of storing a silently wrong point.

## Consequences

- Cross-modal matching quality depends on scene structure: flat or structure-free inputs
  produce explicit refusals, which is the desired behavior (no hallucinated alignments).
- The stack solver is translation-only by design; per-pair affine/polynomial work stays in
  `rs:register_images`, and a global bundle adjustment remains future work.
- CE90 below 20 inliers is flagged degraded rather than faked.
- All new code is Qt-free where possible (processing layer uses QString/QJsonObject only),
  dependency-free (no OpenCV/FFT libraries), and deterministic (fixed iteration order, seeded
  RANSAC reused from D14).
