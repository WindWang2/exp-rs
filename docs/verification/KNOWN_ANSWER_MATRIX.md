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

---

## Known-Answer Matrix — R4 Operator-Oracle additions (Track 7, hardening/r4-operator-oracles)

Four suites (derivations inline; every truth independently hand-derivable,
never implementation back-calculation). The 7.0 "zonal not implemented"
scope refusal is superseded: `rs:zonal_stats` and the raster-vector seam
exist on master and are now pinned. The NoData/mask semantic audit backing
these rows: `.planning/rs-operator-oracles-r4/NODATA_SEMANTIC_MATRIX.md`
(115 operator rows).

### test_operator_nodata_semantics.cpp — NoData/mask semantics

| Family | Invariant under test | Derivation | Test |
|---|---|---|---|
| zonal / raster-vector | 17/100 declared-sentinel pixels excluded: count 83, min 18, max 100, mean 59, pop-σ² 574, median 59, nodata 17 | Σ=5050−153=4897=83·59; Σx²=338350−1785=336565=83·4055 | nodata_semantics ✓ |
| focal stats | sentinel neighbour excluded from window mean; sentinel centre → NaN; output declares NaN | window of (4,4) with (5,5) sentinel: 341/8 = 42.625 | nodata_semantics ✓ |
| mask | apply_mask writes the declared sentinel; pre-existing sentinels pass through; downstream zonal mean = 3630/69 | Σ_masked=30·45+60=1410; 5050−1410−10=3630, n=69 | nodata_semantics ✓ |
| QA mask | declared-NoData QA word fails closed to mask=1 (F-OPS-3), outside cloud_and_shadow class selection | SCL 4→0, 8→1, 3→1, declared-ND→1 | nodata_semantics ✓ |
| spectral derivative | declared sentinel behaves like NaN: derivatives touching its band are NaN, untouched pairs keep the closed slope | v_b=1+λ_b/100 ⇒ d1≡0.01; sentinel in band 3 kills pairs (2,3),(3,4) only | nodata_semantics ✓ (R4 fix) |
| enhancement ratio | declared sentinel pair never yields a finite ratio; output declares NaN | 6/3=2 elsewhere; sentinel→NaN | nodata_semantics ✓ (R4 fix) |
| enhancement stretch | holes carry the resolved sentinel AND the band declares it | v=index, sentinel at 0: min 1 max 99, v=50 → 127.5 | nodata_semantics ✓ (R4 fix) |

### test_operator_chain_tolerance.cpp — multi-operator chains

| Family | Invariant under test | Derivation | Test |
|---|---|---|---|
| radiometric chain | MTL DN→radiance (L=0.1·DN+0.2) then NDVI = 1/(2(r+c)+3) per pixel | gains/adds cancel algebraically in (N−R)/(N+R) | chain ✓ |
| change chain | difference = (i+2, 2i+3); normalized diff = (i+2)/(3i+4); CVA = √((i+2)²+(2i+3)²) | after−before, safeDiv ratio, per-pixel magnitude closed forms | chain ✓ |
| terrain chain | z=2x plane, cell 1: slope = atan(2)° ≈ 63.43494882 at every valid centre (Horn 3×3 AND its 2-pixel NoData fallback agree on linear planes); aspect 270°; sentinel centres → NoData, never 0 | Horn kernel exact on linear surfaces; atan2(−dzdx,dzdy)=−90°→270° | chain ✓ |

### test_operator_determinism_digest.cpp — determinism digests (ADR 0124)

| Family | Invariant under test | Derivation | Test |
|---|---|---|---|
| digest (12 cases / 15 operator products) | same input, two in-process runs: ndvi, mndwi, stretch, ratio, focal 300×300, change_difference, change_cva, terrain slope 300×300, zonal CSV (sha256), threshold, spectral derivative, PCA, sar_speckle, mosaic, spectral_similarity → products byte-identical | ADR 0124 serial regression anchor; 300×300 grids cross 256-tile boundaries (halo paths) | digest ✓ |

### test_known_answer_corpus_r4.cpp — operator-level known answers

| Family | Invariant under test | Derivation | Test |
|---|---|---|---|
| threshold | mask = (v ≥ t); NaN/NoData → 255 and excluded from evaluated counts | changeMask predicate pinned; {1,3,5,ND} @ t=4 → 0,0,1,255 | corpus_r4 ✓ |
| SAR calibration | σ⁰ = DN²/A² (linear_power): DN 6→9, 4→4 with A=2; sentinel → NaN declared | sar calibrateDn definition as closed form | corpus_r4 ✓ |
| continuum removal | convex-hull envelope: triangle spectrum ⇒ CR = v/hull; hull vertices → 1 | hand-traced upper hull (400,0.2)-(700,0.8)-(1000,0.2) | corpus_r4 ✓ |
| spectral resample | linear interpolation onto explicit targets; out-of-range → NaN | 500/600/700 carrying λ/100−4: 550→1.5, 650→2.5, 450→NaN (#445) | corpus_r4 ✓ |
| spectral similarity | SAM labelling picks the angularly closest reference; declared-sentinel pixels unlabelled (−9999) | refs [2,1]/[1,2] are self-labelled at angle 0 | corpus_r4 ✓ (R4 fix) |
| mosaic | overlap is last-valid-wins; NoData never overwrites valid; output declares | B=1000+i over A=i, B(0,0) sentinel → out(0,0)=0, out(42)=1042 | corpus_r4 ✓ |
| extract bands | verbatim pixel copy; sentinel re-declared per output band | exact vector equality | corpus_r4 ✓ |
| mask declaration | explicit no_data on undeclared bands is applied and declared | masked→−7 with declaration; counterpart refusal pinned in refusals suite | corpus_r4 ✓ |

Run: `ctest -R "operator_nodata|operator_chain|operator_determinism|known_answer_corpus_r4" -j1`
(needs the `sicnu_processing`/`sicnu_operators` closure). R4 defect fixes
regression-pinned here: rs:spectral_derivative, rs:spectral_similarity,
rs:image_enhancement (ratio masking + declarations), rs:contrast_stretch
(via band_tools), rs:sar_phase_filter, rs:register_images,
rs:sar_polsar_decompose.
