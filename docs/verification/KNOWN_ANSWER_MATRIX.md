# Known-Answer Matrix — Verification 7.0

Small, analytically derivable fixtures asserting numerical/physical
invariants. Derivations live inline next to each assertion in
`tests/test_known_answer_corpus.cpp`. Existing per-family suites are listed
for dedup; this track adds invariants that were missing, not duplicates.

| Family | Invariant under test | Derivation | Test |
|---|---|---|---|
| spectral (NDVI) | gain invariance: ndvi(g·N, g·R) = ndvi(N,R), g>0 | algebraic cancellation of g in (N−R)/(N+R) | known_answer ✓ |
| spectral (NDVI) | range [−1,1]; N=R ⇒ 0 | ratio bounds; zero signal | known_answer ✓ |
| spectral (SAVI) | SAVI = 1.5·(N−R)/(N+R+0.5) at unit scale | formula pinned incl. #680 (1+L) convention | known_answer ✓ |
| spectral (EVI/NDWI/…) | basic formulas | existing | `test_spectral_indices`, `test_band_ratio` [existing] |
| change | difference(after=before) ≡ 0; σ=0 | algebraic identity | known_answer ✓ |
| change | ramp 1..7: mean 4, σ=2 (population) | Σ and Σx² closed forms; kernel is population-normalized (pinned) | known_answer ✓ |
| change | mask = signed diff ≥ threshold (inclusive) | documented dialog contract | known_answer + `test_change_detection` |
| terrain (slope) | inclined plane z=2x, cell 1 ⇒ slope = atan(2) everywhere interior | Horn 3×3 kernel is exact on linear surfaces | known_answer ✓ |
| terrain (aspect) | rising eastward ⇒ aspect 270° (compass, cw from N); flat ⇒ −1 | Horn gradient direction + documented convention | known_answer ✓ |
| terrain (hydrology/SAR) | D8 flow, speckle kernels | existing | `test_terrain_foundation5`, `test_sar_kernels`, `test_sar_foundation5` [existing] |
| radiometric | L = gain·DN + bias | Landsat RADIANCE_MULT/ADD definition | known_answer ✓ |
| radiometric | ρ = (mult·DN + add)/sin(θ); θ=90/30/45 ⇒ 1, 2, √2 | Landsat TOA definition; pins the sun-elevation division | known_answer ✓ |
| radiometric | T = K2/ln(K1/L+1) round-trips 300 K | Planck trace inversion (Landsat 8 b10 constants) | known_answer ✓ |
| classification | transition matrix counts + marginals + NoData mask skip | hand-computed 2×2 example | known_answer ✓ |
| classification metrics | accuracy/kappa | existing | `test_accuracy_assessment` [existing] |
| temporal stats | Welford {1..8}: mean 4.5, pop-σ² 5.25, sample-σ² 6.0 | closed forms | known_answer ✓ |
| temporal | regression recovers y=2t+1 (slope 2, intercept 1, r² 1) | noiseless-line fit | known_answer ✓ |
| temporal (fit/trend) | piecewise trend RMSE denominators | existing (#759) | `test_temporal_fit` [existing] |
| zonal / raster-vector | — | **not implemented** in the platform; per task scope ("如已实现") nothing to test; do NOT build features in this track | n/a |
| resampling/warp | kernel correctness | existing | `test_image_warper`, io track grid placement (cloud-io-7) |

Run: `ctest -R test_known_answer_corpus` (needs `sicnu_processing` closure).

---

## Known-Answer Matrix — Verification Platform 8.0 additions

`tests/test_known_answer_corpus_8.cpp` — gaps closed from the 7.0 matrix
(derivations inline; zonal statistics remains refused-by-scope, not missing
test coverage):

| Family | Invariant under test | Derivation | Test |
|---|---|---|---|
| grid ops (window) | 10·row+col grid survives readWindow exactly; sub-window algebra | stored Float32 values are exact doubles at these magnitudes | known_answer_8 ✓ |
| grid ops (budget) | windowByteBudget = w·h·bands·8; exceeding it is typed GeoError(Unsupported) | documented #808 contract; budget−1 throws, exact succeeds | known_answer_8 ✓ |
| grid ops (blocks) | tiled edge blocks return full blockW·blockH geometry padded with band NoData (−9999) | 6×4 raster, 4×2 blocks → 2×2 block grid; interior/edge blocks hand-computed (#790 as closed form) | known_answer_8 ✓ |
| splits (random) | N=20 @ 0.5/0.25/0.25 → exactly 10/5/5; disjoint roles; same seed ⇒ same manifest | largest-remainder with no residue; determinism contract | known_answer_8 ✓ |
| splits (remainder) | N=10 @ thirds → 4/3/3 (remainder to TRAIN, never Test); testRatio=0 ⇒ 0 Test | #788 largest-remainder rule as corpus form | known_answer_8 ✓ |
| splits (spatial) | four 5-sample quadrant clusters, 100×100 blocks → whole-block allocation; no block straddles roles; counts 10/5/5 | block-atomic partition (#775/#817 as corpus form) | known_answer_8 ✓ |
