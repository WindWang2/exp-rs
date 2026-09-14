# REVIEW_LOG — D15 Classification & Change Detection Studio

## Round 1 (Phase 6): dual-axis review by 2 read-only subagents

- Axis 1 (Standards): **78/100** pre-fix. 3 × P1, 2 × P2, 13 × P3.
- Axis 2 (Spec/Physics): **88/100** pre-fix. 1 × P2, 9 × P3; 5 closed-form
  truths independently recomputed and CONFIRMED (Moran 3(3−√2)/14, GLCM
  stripe + omni + top-bin clamp, canonical confusion matrix).

## Findings and resolutions

| # | Sev | File | Finding | Resolution |
|---|-----|------|---------|------------|
| 1 | **P1** | classifier_engine.cpp (NB `choleskyInvert`) | Second triangular substitution iterated rows ASCENDING, reading not-yet-computed entries — `covInverse` was silently `L⁻¹` instead of `(LLᵀ)⁻¹` whenever covariances are correlated (fixtures had independent dims so tests passed). | Fixed to DESCENDING order; NEW regression test realizes exact correlated covariance [[1,−0.5],[−0.5,1]] from hand-crafted 3-sample classes and asserts the hand-computed posterior 1/(1+e⁻⁸) = 0.9996646 (wrong order gives 0.997). |
| 2 | **P1** | glcm_texture.cpp `quantize` | `static_cast<int>(floor(t))` with unbounded t is UB ([conv.fpint]) for extreme radiance. | Clamp to [0, G−1] before the cast. |
| 3 | **P1** | classification_studio_widget.cpp scatter binning | NaN/inf features hit the float→int bin cast (UB); NaN is a legitimate GLCM output. | Non-finite samples skipped at binning. |
| 4 | **P2** | classification_postprocess.cpp sieve | Simultaneous rewrite of adjacent small components can swap classes and oscillate to the pass cap (repro `{5,7}` 1×2, min size 4). | Rewrite targets now resolve through neighbour chains/cycles: chains follow to the final large component or NoData; cycles collapse to the lowest class id; pass cap retained as safety net. |
| 5 | **P2** | classification_tool.cpp `execute` | Confusion matrix expanded into per-count sample vectors — O(Σcounts) memory on agent input. | Matrix-native `ConfusionMatrixEvaluator::finalize` path. |
| 6 | P2 | classification_studio_widget.h | "bounded by maxPixels" claim covered the BFS, not the full-plane read. | Header states the full-layer materialization explicitly. |
| 7 | P2 | spatial_split.cpp | `variance` named but never normalized (Σ, not mean); guard is spec-literal fixed 1e-12. | Renamed `centeredSumSquares`, guard documented as ADR-literal (spec formula wins). |
| 8 | P3 | classifier_engine.cpp | `kMaxClusters` declared, never enforced. | Split guard now caps total clusters at 32. |
| 9 | P3 | classifier_engine.cpp | KMeans reseed could share the convergence-break iteration; ISODATA all-undersized fallback planted a zero centroid. | Reseed skips the break; fallback uses the global mean. |
| 10 | P3 | change_detector.cpp | `jacobiEigen` promised sorted eigenvalues (false) and returned a dead `bool`. | Comment corrected; returns void. |
| 11 | P3 | change_detector.cpp log-ratio | t1=t2=−ε produced 0/0 → NaN, violating "identical inputs → 0" beyond the positive domain. | Non-positive shifted arguments collapse to the neutral 0 (domain documented). |
| 12 | P3 | glcm_texture.cpp map | Header said clamp *replication*; implementation truncated border windows. | True replication implemented (full odd square, edge-clamped samples). |
| 13 | P3 | classification_tool.cpp | `|Σ1|`,`|Σ2|` path always applies ≥1e-9 ridge (small systematic bias vs the ridge-free pooled path). | Accepted, documented: degenerate-input safety is worth the consistent bias; pooled path is ridge-free-first. |
| 14 | P3 | tests | Missing `<limits>`; `M_PI` without MSVC guard. | Added. |
| 15 | P3 | headers | "snap to nearest odd" / "4 or 8" doc vs actual snap-down / `!=4 → 8`. | Doc states the actual rules; sieve NoData sentinel (−1, spec signature has no config) documented with the pipeline shift idiom. |
| 16 | P3 | widget | Dead `mXLabel/mYLabel` members; labels are host chrome. | Members removed; signature unchanged (spec). |
| 17 | P3 | d15_e2e_pipeline.cpp | Pointless full-scene plane copies before CVA. | Direct pointers. |
| 18 | P3 | widget cpp | `const_cast` for `dataProvider()`; O(\|train\|×\|eval\|) guard sweep; SVM Gram O(m²). | Kept, each documented at the seam (QGIS accessor is not const-marked; workbench sample caps; SVM intended for ≤10⁴-row pairs). |

**Post-fix status: P0 = 0, P1 = 0.** Full D15 suite re-run green after fixes (62/62).

## Deviations register

1. **Magic-wand polygon**: crack-following boundary instead of the prose's
   convex hull — the spec's own acceptance window ([1240, 1270] px, zero
   background leaks) is only satisfiable by a boundary-faithful outline.
2. **E2E CVA α = 4.0** (DECISIONS I1): unchanged-pixel magnitudes are
   chi(4)-distributed; the Gaussian 1.5σ heuristic sits in the fat tail
   (~3% false alarms → Dice ≈ 0.6); 4σ → ~5e-6.
3. **Red/green pipelining**: the 6259-target baseline build blocked test
   execution for hours; green implementations were typed ahead, but every
   package ran stub-red before implementation-green and committed only
   after both (evidence per package below).  Two packages additionally
   caught real defects in the green step (Package C: missing isTrained
   overrides; Package G: iterator-invalidation segfault).
4. **`~/.local/bin/{cmake,ctest}` shims**: broken user-path shadows that
   launch an unrelated GUI app; absolute tool paths throughout (BASELINE.md).
5. **Raw `const float*` signatures (Packages B/E)**: spec-mandated; kept
   even though newer D15 seams use `std::span`.

## Rubric self-assessment (post-fix)

| Dimension | Weight | Score | Notes |
|---|---|---|---|
| Public seams & contracts | 20 | 18 | Minimal seams at spec paths; additive-only interface extensions (clusterCentroids/isTrained/seed); raw-pointer signatures are spec-mandated. |
| Vertical slices & tracer discipline | 20 | 18 | Per-package stub-red → green → atomic commit; typed-ahead implementations never bulk-committed (deviation 3). |
| Ground-truth independence | 20 | 19 | Every expected value hand-derived or a committed fixture; 3 derivation slips caught and corrected by independent recomputation; two veto-class anti-patterns structurally absent. |
| Black-box decoupling | 20 | 19 | Public-API-only tests (findChild/QSignalSpy are Qt's public surface); no internal mocking; real rasters end-to-end. |
| YAGNI & standards | 20 | 18 | All P1s and the substantive P2 fixed with targeted tests; remaining P3s documented with rationale. |
| **Total** | **100** | **92** | P0 = 0, P1 = 0. |
