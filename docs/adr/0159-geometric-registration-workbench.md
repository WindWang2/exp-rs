# ADR 0159: Geometric Registration Workbench — GCP Analysis, Closed-Form Transforms, TPS, RANSAC Matching, Resampling Pipeline & Pan-Sharpening

Status: accepted · Branch `zcode/geometric-registration-workbench` ·
Baseline `origin/master@007e70cff6` · Owner: processing/agent/app (D14 track)

## Context

The studio already ships a QGIS-ecosystem georeferencing session
(`src/app/georeferencer/rs_georeferencing_session.*`, ADR 0020/0027/0028) that
delegates GCP transformation and warping to `qgis_app_georef`, and a dual
viewport pan/zoom sync controller (`src/app/shell/rs_dual_viewport_sync_controller.*`).
What it lacks is a self-contained, unit-testable numeric layer for D14:

1. GCP spatial-distribution analytics (convex hull coverage, Clark-Evans
   nearest-neighbour index, Bowyer-Watson Delaunay quality metrics) that drive
   teaching-grade scoring;
2. closed-form transform solving (translation/rigid/similarity/affine via SVD
   pseudo-inverse, P2/P3 polynomials with Hartley pre-conditioning, projective
   DLT) with condition-number health reporting — independent of the QGIS
   transformer plugins;
3. thin-plate spline non-rigid correction with regularization and bending
   energy;
4. a deterministic RANSAC homography estimator for automated feature matching;
5. a resampling pipeline (nearest / bilinear / Keys a=-0.5 cubic / Lanczos-3)
   with NoData-weight renormalization instead of sentinel leakage;
6. pan-sharpening (Gram-Schmidt / Brovey / IHS / HPF) with Wald-protocol
   quality metrics (ERGAS, CC, SSIM);
7. a dual-window georeferencing workbench UI with reentrancy-guarded sync;
8. an agent-facing geometric registration tool.

`src/core/` is the vendored QGIS source tree and is off-limits for `rs::` code;
`src/workflow/` (D17), `src/dataset/split.h` (D15) and radiative transfer (D13)
are out of scope.

## Decision

1. **Placement & namespaces.** All numeric modules live in
   `src/processing/algorithms/` inside `sicnu_processing` under `rs::core`
   (GcpManager) and `rs::algorithms` — consistent with existing `rs::algorithms`
   (`band_math_simd.h`) and `rs::display` precedent. The agent tool lives in
   `src/agent/tools/` inside `sicnu_agent` (`rs::agent`, which already PUBLIC-links
   `sicnu_processing`). The workbench window lives in `src/app/workbench/`
   (`rs::app`).
2. **GCP lifecycle.** `GcpPoint` is a value struct; `GcpManager` owns the single
   authoritative vector. Mutators (`add/remove/update/setPointEnabled/clear`)
   emit no signals (pure domain object); residuals are written only through
   `updateResiduals`, which requires exactly one transformed coordinate pair per
   *active* point and recomputes per-point residual magnitude and global RMSE
   (√(RMSEₓ²+RMSE_y²)). Disabled points are excluded from every metric.
3. **Distribution analytics.** Andrew monotone chain hull O(N log N) +
   shoelace area (<3 active points ⇒ area 0); Clark-Evans R = r̄_A/r̄_E with
   r̄_E = ½√(W·H/N), undefined (<2 points or zero image area) ⇒ 0;
   Bowyer-Watson Delaunay with a super-triangle, worst aspect ratio
   R_circum/(2·r_in); duplicate coordinates rejected at add time.
4. **SVD pseudo-inverse.** One-sided Jacobi SVD, no external dependency.
   Singular values below 10⁻¹²·σ_max are truncated to zero. κ(A)=σ_max/σ_min;
   `success=false` when κ>10¹⁴ or the system is underdetermined. Polynomials
   P2/P3 assemble normal equations in Hartley-normalized coordinates
   (centroid→origin, mean distance→√2) per output dimension and unscale the
   coefficients afterwards; reported coefficients are always in real image
   coordinates. DLT solves the 2N×9 homogeneous system for the smallest
   singular vector.
5. **TPS.** U(r)=r²·ln r with r≤10⁻¹² short-circuited to 0. The (N+3) augmented
   system [K+λI, P; Pᵀ, 0] is solved by partial-pivot LU per output dimension.
   Knots closer than 10⁻⁹ are deduplicated before assembly. Bending energy is
   WᵀKW/(16π). `transform` on an unfitted interpolator returns the input
   unchanged (identity) rather than UB.
6. **RANSAC.** `std::mt19937(42)` deterministic sampling; 4-point DLT
   hypothesis; forward reprojection error against `reprojThreshold`; adaptive
   iteration cap ln(1-p)/ln(1-(1-η)⁴) bounded by `maxIters`; final least-squares
   refit on all inliers. Fewer than 4 candidates ⇒ identity matrix + all-outlier
   mask. Denominators |w|<10⁻¹² yield ±infinity sentinels, never exceptions.
7. **Resampling.** Reverse (target→source) scanline warping only, so output has
   no holes by construction. Kernel neighbourhoods that hit NoData renormalize
   over valid weights and emit NoData when valid weight share <50%. Cubic
   kernels must satisfy partition of unity (ΣW=1, tested to 1e-12);
   `clampRange=true` hard-clips cubic/Lanczos overshoot to [minValue,maxValue].
   Memory bound: one source row window + one output row; no intermediate full
   raster.
8. **Pan-sharpening.** Simulated low-res pan = weighted sum of upsampled MS
   bands (default equal weights, overridable). Classical Gram-Schmidt forward
   orthogonalization, PAN mean/variance histogram match onto GS₁, then inverse
   projection. Variances below ε=10⁻⁷ are clamped. Wald-protocol metrics
   (ERGAS, per-band CC, RMSE, global-form SSIM) are computed from the degraded
   fusion against the original MS.
9. **Dual-window workbench.** `GeorefDualWindow` mirrors the proven
   `RsDualViewportSyncController` pattern: `QPointer<QgsMapCanvas>` on both
   canvases, `mApplyingSync` reentrancy guard, 16 ms `QTimer` throttle,
   extent-preserving-scale sync, GCP table ↔ model re-fit wiring, and test
   inspection seams (`gcpTableRowCount`, `displayedGlobalRmse`,
   `isApplyingSync`) usable headless under `QT_QPA_PLATFORM=offscreen`.
10. **Agent tool.** `GeometricTool::execute` never throws across its boundary;
    every response is the `{success, action, data, diagnostic_message}`
    envelope with snake_case keys. The 3σ residual audit flags
    r_i > r̄+3σ_r (or r_i > 3·RMSE) and predicts post-removal RMSE; the model
    recommender implements the spec decision tree (N<6 ⇒ affine; ≥6 & flat &
    coverage>0.7 ⇒ P2; ≥10 & rough ⇒ TPS/P3).
11. **Teaching labs.** `data/labs/lab06/lab07` JSONs are schema-locked
    teaching specs owned by the lab-content track and are not modified. The D14
    100-point rubric (lab 06: GCP count 10 / coverage 20 / Clark-Evans 10 /
    RMSE 40 / clean resample 20; lab 07: resolution 20 / CC 40 / ERGAS 40) is
    computed live in the e2e test from production metrics only.

## Consequences

- Nine new Catch2 targets, all green offline and headless; no QGIS behaviour
  changes; vendor tree untouched; zero new third-party dependencies.
- The QGIS-based georeferencer remains the interactive production path; the
  D14 numeric layer is additive and can later back both the workbench and the
  agent without cross-dependencies.
