# Geometric Registration — Multimodal Matching, RPC Bias, Stack Adjustment & Quality (F13)

Scope: `src/processing/algorithms/registration/` (namespace `sicnu::registration`), the
`rs:register_images` / `rs:stack_register` operators, the `spatial:geometric_registration`
agent tool surface, and the RPC bias layer consumed by
`src/analysis/georeferencing/qgsrpcgcptransformer`. Builds on the D14 capabilities documented
in ADR 0159 (GCP management, closed-form transforms, TPS, RANSAC matching, resampling).

## Refusal semantics (read this first)

Every registration product carries a `RegistrationStatus`:

| status | meaning | caller obligation |
|---|---|---|
| `success` | evidence supports the transform | use |
| `low_confidence` | transform produced but below trust gates (coverage/consensus) | review; product is flagged, not hidden |
| `refused` | structural shortfall — no trustworthy geometry exists | do not use; no output written |
| `failed` | internal error (I/O, allocation) | retry/diagnose |

Stable reason codes (snake_case, surfaced verbatim by UI/agents):
`too_few_matches`, `flat_region`, `low_peak_snr`, `insufficient_coverage`,
`low_consensus`, `degenerate_geometry`, `ill_conditioned`, `model_not_justified`,
`cancelled`, `cap_exhausted`, `io_error`.

## Cross-modal matching (`MultimodalMatcher`)

- `matchImages(src, dst, options, cancel)` — row-major float buffers, NaN = NoData.
- Metrics: `PhaseCorrelation` (windowed FFT cross-power spectrum; gain-invariant), `MutualInformation`
  (16-bin robust quantization; monotone-remap invariant — the optical-SAR default at the finest
  level), `NormalizedCrossCorrelation` (radiometrically similar sensors), `Auto` = phase
  correlation down the pyramid + MI at the top.
- Coarse-to-fine: [1,2,1]/4 NaN-aware pyramid; robust per-level median offset; per-window
  metric-scan fallback when the phase peak fails.
- Trust gates: per-point score floor, peak-SNR floor, valid-fraction floor (NoData), RANSAC
  homography consensus, full-extent coverage (a clustered match set scores low — by design),
  inlier-ratio floor.
- Deterministic: fixed iteration order; the only randomness is D14's seeded RANSAC (mt19937/42).

## Model selection (`ModelSelector`)

Ladder Translation → Similarity → Affine → Projective → Polynomial2 → Polynomial3 → Tps.
A more complex model is adopted **only** if its k-fold held-out RMSE improves on the current
selection by ≥ `minImprovement` (default 10%) and κ stays under 1e14. The full per-candidate
evidence table (fit RMSE, held-out RMSE, κ, bending energy, rejection reason) is returned for
explainability — nothing is hidden behind the verdict.

## RPC bias refinement (`RpcBiasModel`)

Inputs are per-GCP bias samples (observed − RPC-predicted ground position). The layer fits:

- `Constant`: median bias (robust to outliers; preserves the pre-F13 refinement behavior);
- `Affine`: 6-parameter ground-space bias field, adopted only with ≥6 samples and a ≥10%
  held-out improvement over constant.

`heightSensitivity()` turns a caller-supplied reprojector into per-GCP dGround/dHeight
statistics for the quality report. GDAL RPC coefficients are never modified (ADR 0057).

## Stack registration (`StackRegistrator`)

`solveTranslations(sceneIds, observations)` solves one global 2-DoF translation adjustment
(reference pinned, confidence × inlier-count weights, dense normal equations, ≤256 scenes).
Disconnected scenes are reported (`hopCount = -1`), never silently dropped. Drift is reported
as max/RMS post-adjustment edge residuals — an inconsistent triangle cannot hide. Global
affine/bundle adjustment is explicitly not-supported (see CAPABILITY matrix in the track
planning notes).

## Quality products (`RegistrationQuality`)

- Radial RMSE and **CE90** as the empirical 90th percentile of radial residuals; below
  20 inliers the value is flagged `ce90Degraded` (worst observed error) alongside the
  Rayleigh reference 2.146·σ.
- Residual vector field: grid over the full source extent, per-cell signed mean vectors.
- Local confidence per tie point: score × exp(−residual/RMSE) × neighborhood support ×
  **coverage factor** (clustered sets cannot score high).
- Schema `exp_rs_registration_quality/1` JSON, written atomically (QSaveFile).

## Surfaces

- **Operators** (headless pipelines / CLI): `rs:register_images` (match → affine fit →
  reverse-mapped warp into the reference grid → GeoTIFF + optional quality sidecar;
  refusal throws and writes nothing), `rs:stack_register` (adjustment + sidecar).
- **Agent tool** `spatial:geometric_registration`: actions `audit_residuals`,
  `recommend_model`, `inspect_misalignment` (D14), plus `multimodal_register`,
  `select_model`, `stack_register` (F13). Refusals travel inside `data.status`/`data.reason`
  — the tool call itself succeeds so the agent can read the refusal.
- **Interactive UI**: unchanged dual-window workbench (D14/D18); GCP picks that cannot be
  transformed into the layer CRS are refused with a status-bar message (#1005, fail-closed).

## Honest limitations

- The multimodal matcher matches **windows**; it is not a general feature detector and does
  not replace SIFT for same-modality keypoint work.
- Stack adjustment is translation-only at the global level.
- CE90 with small samples is flagged, not invented.
- No new third-party dependencies: FFT, MI, and solvers are in-repo and deterministic.
