# REVIEW R1 — r33-t01-science (9 fix commits + tests)

Reviewer: R1 (independent, adversarial). Date: 2026-11-18.
Branch: `zcode/r33-t01-science-20261117T150207Z` on `origin/master` `4e1d664d47`.
Scope: full diff read; every changed source file read in full; mathematics re-derived by hand; all new tests read; **18 test executables built and run in `build-dev` — all green** (see §Execution).

Note: the task brief said "7 new test files"; the branch actually adds **9** (`test_r33_t01_{polsar_helix, geocode_beam, network_velocity, sentinels, speckle_multitemporal, landform_evidence, hillshade_nodata, resolveflats, median_large}.cpp`) plus edits to `tests/test_sar_polsar.cpp`. All 9 reviewed.

---

## Verdict per claimed fix (all verified independently)

### 1. Yamaguchi4 helix decontamination |fh|/4 (#2127) — CORRECT
`src/processing/algorithms/sar/sar_polsar.cpp:216-229`, `sar_polsar.h:31-46`, `docs/processing/sar-domain.md:199-210`.

Hand-derived the helix covariance for both handednesses. With u = [1, j, −1]/2 (right):
u·u^H = ¼·[[1,−j,−1],[j,1,−j],[−1,j,1]] → diagonal ¼ each, C12 = C23 = −j/4, C13 = −¼.
Left helicity u_L = conj(u): C12 = C23 = **+j/4**, C13 = **−¼ (unchanged)**, diagonal ¼ (unchanged). So with fh = −4·mean(Im C12, Im C23) (fh > 0 right, fh < 0 left, matching the header), the physical helix covariance is |fh|·(u u^H or conj) — diagonal |fh|/4 and Re C13 = −|fh|/4 in **both** handednesses; only Im C12/C23 carries the sign. The header's `|fh|·conj(u)·conj(u)^H` notation is correct: conj(u)·conj(u)^H = (u u^H)^T = conj(u u^H) for the Hermitian outer product.

Mixed-target SPAN check (unclamped): C = Cs+Cd+Cv+Ch gives fv = 1.5(C22 − |fh|/4) exact, r11/r33 = fs/fd-blocks exact, r13 = C13 − fv/3 + |fh|/4 exact; Ps+Pd+Pv+Ph = trace-3 = C11+C22+C33 exactly in both handednesses (verified numerically for the test's fixture: 2.2 + 8/3 + 0.6 = 5.4667 = trace). The old signed `fh/4` inflated volume and residuals for fh < 0 — the fix direction is right. `out->helix = |fh|·3/4` (pre-existing) is the trace-3 helix power, consistent with the other powers.

Tests: `test_r33_t01_polsar_helix.cpp` builds PSD rank-1 targets through the production accumulate/finalize path (never hand-written negative diagonals — the old `test_sar_polsar.cpp` left-helicity fixture used fh/4 = −0.2 diagonals, a non-physical covariance; it is fixed in this diff and now also pins SPAN). Hand-derived expectations (0.6/0/0/0, mixed-target exact recovery, clamp boundary) + registry E2E with windowSize 1. Independent of the implementation. Good.

### 2. geocodeGroundCell beam-away ENU projection (#2126) — CORRECT
`src/processing/algorithms/sar/sar_geocoding.cpp:189-213`.

g must be the ENU components of −(sh − (sh·u)u). Since u·e = u·n = 0, the e/n components of sh are sh·e and sh·n and the horizontal projection only drops the up component: gx = −(shx·ex + shy·ey + shz·ez) (ez = 0, harmless) and gy = −(shx·nx + shy·ny + **shz·nz**) — the old code omitted shz·nz; near the equator nz = cos φ ≈ 1 so the north component lost the (dominant) radial look component. Fix is the correct full dot product.

Threshold logic re-derived: dR/dd along the unit beam-away horizontal direction = cos θe − sin θe·tan α (θe = look elevation); fold ⟺ dR/dd < 0 ⟺ α > 90° − θe (layover); illumination shadow ⟺ α < −θe. These match `sar_terrain_geometry.cpp:51-66` exactly (θI = 90° − θe: layover α > θI, shadow α < θI − 90°), so the "reduces to the constant-geometry conditions" claim holds.

Test: `test_r33_t01_geocode_beam.cpp` pins absolute classes (Shadow for dzdN ∈ {0.8, 1.0, 1.2} — the legacy projection kept these Normal) plus an independently constructed ENU oracle and independent facet/local-incidence math. Good.

### 3. Anchored velocity OLS (#2128) — CORRECT
`src/processing/algorithms/sar/sar_network_inversion.cpp:286-312`.

With the anchor (t0, 0) included as a data point (n = dim+1, anchor contributing t0 to Σt, t0² to Σtt, 0 to Σu/Σtu), the OLS slope is (n·Σtu − Σt·Σu)/(n·Σtt − Σt²) — exactly what the code computes. The anchor is justified by the datum convention (epochDisplacement[0] ≡ 0 set at line 242): the reference epoch's displacement is a known observation, so including it is consistent; solved-only regression both NaN'd two-epoch pixels (den = t²−t² = 0) and biased slopes when the reference epoch carries information (hand check: u = [0,1,0,1] mm on t=(0,1,2,3) → solved-only slope 0, anchored 1/5 = 0.2 mm/yr, matching the test). Time-origin invariance is a standard OLS property and is pinned by the test with a +7.3 yr shift. `solvedEpochs` still counts non-reference solved epochs (unchanged); `droppedPairs`/RMS semantics untouched; the 1e-30 denominator guard retained; epoch 0 never appears in solvedEpochs so the anchor cannot double-count.

Per-pixel mask/NaN handling: displacement rows NaN-ed at sentinels (operator) flow into `solvePixel`'s `isfinite` mask build; per-pattern union-find and reference-component selection untouched. The operator change (`rs_sar_network_inversion_operator.cpp:306-322, 447-462`) NaN's sentinel samples before `allValid`/intersect logic — maskStrategy semantics preserved.

Tests: `test_r33_t01_network_velocity.cpp` — hand-derived 0.2 mm/yr, two-epoch 5 mm / 2.5 yr = 2 mm/yr, index-fallback, E2E raster. Existing `test_sar_network_inversion.cpp` / `test_sar_platform11.cpp` fixtures are exactly linear so the anchor does not change their pinned slopes — both still pass (run).

### 4. Finite NoData sentinels (#2102) — CORRECT
- `rs_sar_displacement_operator.cpp:144-147, 178-222`: sentinel equality is on the RAW float band value before any conversion (this operator applies no scaling); sentinel pixels write NaN, are excluded from `validPixels`, and `sampleValid()` excludes them from BOTH members of each Itoh pair (right/down stay core-restricted, halo never pairs). Counter `noDataSentinelPixels` counts core pixels exactly once.
- `rs_sar_remove_topographic_phase_operator.cpp:393-397, 486-498`: DEM sentinel NaN'd on RAW units BEFORE `demUnitScale` multiplication — the order is load-bearing (a −9999 cm sentinel must not become −99.99 m of relief) and is correct; the fail-closed bilinear sampler then propagates NaN at the hole. Counter caveat: see P3-1.
- `rs_sar_network_inversion_operator.cpp`: per-pair sentinel (double→float cast, exact-equality — the repo-wide `bandNoDataSentinel` pattern, `nodata_utils.h`); sentinel → NaN row → perpixel drops the row / intersect drops the pixel; counters added.

Sentinel-cast note: `static_cast<float>(declared)` vs float32 pixel reads round identically for any declaration of a float32-stored value; NaN-declared sentinels fall back to the non-finite path (correct). A legitimate sample equal to a finite sentinel is indistinguishable — inherent to finite sentinels, documented platform convention.

Tests: `test_r33_t01_sentinels.cpp` — per-pair DISTINCT sentinels (proves per-band reads), perpixel vs intersect with counter pins, exact displacement conversion −λφ/4π, cm-DEM scaling-order E2E with NaN at the hole and 24 valid neighbours. Good.

### 5. Multitemporal speckle contract (#2129) — CORRECT
`src/processing/algorithms/sar/sar_speckle.cpp:245-364`.

- Per-scene sentinel arrays sized/indexed by companion index, resolved via `bandNoDataSentinel(companions[i], band)`; the reference scene keeps the operator-resolved per-band `nodata` (#803 pattern). `accumulate` gates both the centre tap and the local-statistics taps on the scene's own sentinel (`hasSentinel = isfinite(sceneSentinel)` — undeclared → NaN sentinel → no false drops). Verified: NaN sentinel never equals a finite tap.
- Grid gate: `compareGrids(...).compatible()` = "no blocking issue" (MissingCrs/CrsMismatch/axis/rotation/pixel-size/origin/extent block; NoData mismatch is a warning; two unreferenced grids compare Compatible and fall back to the existing WxH check — `raster_grid_compat.h:59-83`). Correct semantics for a lockstep tile walk; 1e-9 relative tolerance avoids false refusals.
- Band check `band > companions[i].bandCount()` with 1-based `band` (operator guarantees band ≥ 1) — correct.
- `count[i] == 0 → NaN` — correct under the NaN-declared output; the old code wrote the reference's finite sentinel back as signal.
- Non-multitemporal paths (lee/enhanced_lee/frost/kuan/gamma_map/refined_lee) are byte-for-byte untouched (the switch block and the sentinel pre-pass below it are unmodified); `test_speckle_filter` (15869 assertions) passes.

Tests: `test_r33_t01_speckle_multitemporal.cpp` — hand oracle (uniform scenes → exact temporal means), per-scene sentinel DIRECTION pin (a companion pixel equal to the REFERENCE's sentinel stays valid data — output −9999 exactly), grid-mismatch and band-shortage refusals. Good.

### 6. Landform evidence gate (#2091) — CORRECT
`src/processing/algorithms/terrain_landform.cpp:121-131, 183-196`, `terrain_landform.h:64-67`.

Gate fires only when (a) `slope()` emitted nodata — which per PROC-A2-4 happens only when BOTH E/W or BOTH N/S neighbours are invalid (no gradient evidence on an axis), never for all-valid planes (border pixels of valid DEMs get one-sided differences) — or (b) the TPI window had `count < 1` valid neighbours. A legitimate flat plane keeps slope 0 (valid) and TPI 0 (valid, SD 0 → stdTpi 0) → class 0 plains; the existing plateau pin in `test_terrain_landform.cpp` still passes. No-evidence cells are 255 (operator maps 255 → nodata, `rs_terrain_landform_operator.cpp:356`); a flat plane's exact 0 TPI stays distinguishable from the nodata sentinel. Header contract updated consistently. No other caller depends on the old "TPI 0" fabrication (checked `rs_terrain_landform_operator.cpp`).

Tests: `test_r33_t01_landform_evidence.cpp` — isolated pixel 255, single-hole plane stays plains, NoData-sandwiched strip 255 (pre-fix plains), tilted plane keeps a non-plains non-255 class with hand-checked Horn slope 5.7106°. Hand-derived; good.

### 7. hillshadeMultidirectional per-direction validity (#2094) — CORRECT
`src/processing/algorithms/terrain_analysis.cpp:736-779`.

The old `out[i] += shade[i]/8` turned a finite sentinel into sentinel/8 (not representable in float32 — e.g. −99999.9/8) and the NoData pixel resurfaced as a huge "valid" shade. The fix detects `shade[i] == nodata` exactly per direction, voids `valid[i]`, skips further accumulation for that pixel, and stamps the EXACT sentinel in a final pass. No partial-accumulation hazard: once `valid[i] = 0` the pixel is skipped for the remaining directions and is overwritten at the end. The streaming-reuse contract is preserved (`std::fill(out, out+n, 0)` before the loop — pinned by the poisoned-buffer test). Observation (not a defect): because `hillshade()`'s stencil evidence is azimuth-independent, a direction returning nodata implies all 8 do; the per-direction loop is thus equivalent to a per-pixel check — correct either way.

Tests: `test_r33_t01_hillshade_nodata.cpp` — 8-direction oracle over the public single-direction kernel (the multi kernel's spec IS the 8-way mean), exact-sentinel pins with an unrepresentable-by-8 sentinel, NaN-input case, gamut checks. Good.

### 8. resolveFlats all-NoData initialization (#2095) — CORRECT
`src/processing/algorithms/terrain_hydrology.cpp:189-203`.

The copy loop writes every cell of `filled` verbatim from `dem` (NaN and sentinel values included — pinned by the mixed-invalid test), sets `report->epsilon = 0`, `raisedCells = 0`, returns true. No missed cancellation path: the preceding min/max scan polls `cancelled` every cell; the copy itself is O(n) memcpy-class work with nothing to cancel mid-way. The caller pattern that motivated the bug is real: `rs_terrain_flow_operator.cpp:191` uses a value-initialized `filled` buffer which previously stayed all-zeros on all-NoData DEMs and was ingested as 0 m terrain by D8/D∞.

Tests: `test_r33_t01_resolveflats.cpp` — zeros-buffer contract, mixed invalid passthrough, dirty-buffer reuse, and the PROC-A2-1 high-altitude epsilon pin (z ≈ 4000 m). Good.

### 9. Exact float32 selection median ksize>5 (#2088) — CORRECT
`src/operators/opencv/opencv_filter_operators.cpp:43-84, 266-277`.

- Median definition: odd count → `taps[mid]` after `nth_element` (the element that would be at sorted position mid) — correct. Even count → `0.5·(lower + taps[mid])` where `lower = *max_element(taps.begin(), taps.begin()+mid)`. I proved `max(lower half) == sorted[mid−1]` for all cases including duplicates: elements strictly below `*nth` must all precede nth; the lower half is exactly {all elements < S[mid]} ∪ (mid−c copies of S[mid]), whose max equals S[mid−1] in both regimes. Correct selection median.
- BORDER_REPLICATE: `std::clamp(y+j, 0, h-1)` / `std::clamp(x+i, 0, w-1)` — matches cv::medianBlur's replicate border.
- In-place hazard: `img.clone()` up front; all taps read from the clone, writes go to `img` — safe. `taps` bounded by k² ≤ 101² floats.
- Pre-existing k≤5 path untouched (OpenCV + NaN-mask semantics), pinned by the parity section and `test_opencv_operators.cpp`. Previously ksize 7..101 on CV_32F reached OpenCV's CV_8U-only assertion (runtime error); now valid work. Verified the claim that medianBlur supports CV_32F only at ksize 3|5.

Tests: `test_r33_t01_median_large.cpp` — hand-tallied replicate-weighted multiset (centre = 12 exactly), brute-force spec oracle for every pixel (documented as the same SPEC re-implemented independently — acceptable; the hand-tallied and even-count pins protect against a shared-algorithm bug), k=101 on a 4×4 raster, all-NaN, 1e30 preservation, and k∈{1,3,5} OpenCV parity with mask semantics.

---

## Execution evidence

Built in the worktree's `build-dev` (Debug) and ran — all green:

- New: `test_r33_t01_polsar_helix` (772 asserts), `_geocode_beam` (42), `_network_velocity` (55), `_sentinels` (98), `_speckle_multitemporal` (29), `_landform_evidence` (81), `_hillshade_nodata` (166), `_resolveflats` (96), `_median_large` (181).
- Regression suites around the changed kernels: `test_sar_polsar` (147), `test_sar_geocoding` (5769), `test_sar_network_inversion` (35), `test_sar_platform11` (357), `test_speckle_filter` (15869), `test_sar_operators` (910), `test_sar_kernels` (139), `test_opencv_operators` (1457), `test_terrain_landform` (202), `test_terrain_hydrology` (1023), `test_terrain_foundation5` (277).

## CMake

`tests/CMakeLists.txt:2009-2171`: nine new blocks appended after `test_sar_polsar`; identical shape to neighbours (Catch2WithMain / Qt6::Core / qgis_core / sicnu_processing [/ sicnu_operators] / GDAL::GDAL, `sicnu_link_jsoncpp` exactly on the tests that include `json/json.h`, same include dirs, `sicnu_discover_tests`). No existing block modified. Consistent.

## Edge cases checked with no finding

- Integer overflow: tap buffer and index math bounded (k ≤ 101); tile counts use size_t.
- Empty raster: `medianLargeKernel32F` guards w/h ≤ 0; speckle/network operators reject empty inputs up front.
- Band indices: 1-based checks consistent (`band > bandCount()`).
- Float precision: sentinel double→float casts follow the repo's exact-equality convention; hillshade sentinel is compared exactly and written exactly.
- Cancellation hooks: tpiMultiscale/landformClasses/geomorphon per-scale/row polling preserved; the operator row-level `throwIfCancelled` loops in displacement/topo/network are untouched; resolveFlats copy path analyzed above.
- Buffer aliasing: median clone; hillshade `shade` scratch separate from `out`; network `displacementRow`/`epochDisplacement` distinct.
- Error-path cleanup: topo/network operators abandon outputs and destroy the OCT transform on every new/old failure path (re-verified each throw site); speckle operator `dst.abandon()` on kernel false.
- Docs: `sar-domain.md` §7 helix text updated consistently with the header; §14 wording ("OLS linear velocity") does not contradict the anchor (P3-4 suggests a one-word clarification).

---

## Findings

### P2-1 — Large-kernel median: non-cancellable full-frame O(w·h·k²), doubled memory, and an unnecessary full-frame constraint
`src/operators/opencv/opencv_filter_operators.cpp:43-84` + `OpenCvMedianBlurOperator::neighborhoodRadius` (`:251-258`).
Evidence: `neighborhoodRadius` returns −1 ("FullRaster on purpose: cv::medianBlur's ordering of NaN inside a window is unspecified … so the streamed tiles cannot reproduce the full-frame output"), and the new path runs `medianLargeKernel32F(srcDst, kernelSize)` on the whole band with `img.clone()` and per-pixel `nth_element` — no cancellation or progress inside the band.
Why it matters: the NaN-ordering rationale applies only to the k 3|5 OpenCV path. The new selection median is deterministic and exactly tile-streamable with a k/2 halo (BORDER_REPLICATE taps), so keeping it full-frame forfeits streaming; worst legal invocation (k = 101 on a 10k×10k band ≈ 10¹²+ selection ops) runs for hours, cannot be cancelled (base class polls `throwIfCancelled` only between bands), and the clone doubles peak memory.
Suggested fix: return `kernelSize/2` from `neighborhoodRadius` when `kernelSize > 5` (route to the streaming path), or at minimum thread a cancel/progress hook into `medianLargeKernel32F` and document the cost in the schema.

### P2-2 — New speckle refusal modes surface as an untyped generic "GDAL error"
`src/operators/rs/rs_sar_speckle_operator.cpp:238-244` + `src/processing/algorithms/sar/sar_speckle.cpp:256-273`.
Evidence: companion open failure, grid mismatch (`!report.compatible()`), and band shortage (`band > companions[i].bandCount()`) all `return false`, and the operator wraps every false as `RSOperatorError(ErrorCode::GdalError, "SAR speckle filtering failed while streaming")`.
Why it matters: a user-supplied companion on the wrong grid or without the band now gets a "GDAL error" with no detail — a diagnosability regression risk exactly in the new failure modes, and inconsistent with `rs_sar_network_inversion`, which throws `GRID_MISMATCH` with the per-issue detail (`rs_sar_network_inversion_operator.cpp:290-301`).
Suggested fix: thread a QString error out of `speckleRaster` (or throw typed refusals for the companion gates) distinguishing grid/band/open.

### P3-1 — `noDataSentinelSamples` in remove_topographic_phase counts halo-overlap samples with multiplicity
`src/operators/rs/rs_sar_remove_topographic_phase_operator.cpp:486-498`.
Evidence: `for (float &v : demBuf) { if (… v == demSentinel) { … ++demSentinelSamples; …} }` — demBuf windows of adjacent tiles overlap by their +1 px DEM halo, so a sentinel within 1–2 pixels of a tile boundary is NaN-ed and counted by 2–4 tile iterations. (The displacement operator's `noDataSentinelPixels` counts core pixels exactly once; the network operator's tiles do not overlap.)
Why it matters: the QA number is not a unique-sample/pixel count on multi-tile rasters (the single-tile test cannot catch this). The name says "samples" so it is not wrong, just misleading.
Suggested fix: count on the tile's pixel-center footprint only (or document the multiplicity in the result key description).

### P3-2 — Even-count median mean can overflow to +inf near FLT_MAX
`src/operators/opencv/opencv_filter_operators.cpp:77-79`.
Evidence: `median = 0.5f * (lower + median)` — `lower + median` overflows float for two central taps near FLT_MAX (the odd path returns exact values; only the even average saturates). Theoretical for float32 geodata; noted for completeness.
Suggested fix: `0.5f*lower + 0.5f*median`.

### P3-3 — Inconsistent ±Inf semantics across the ksize 5/7 boundary
`src/operators/opencv/opencv_filter_operators.cpp:61` vs `:278-285`.
Evidence: the new path excludes taps via `std::isfinite(v)` (drops ±Inf); the k ≤ 5 path's mask is `cv::compare(srcDst, srcDst, mask, CMP_EQ)` which treats ±Inf as valid.
Why it matters: a raster with +Inf samples gets different invalid-sample policy at ksize 5 vs 7. For this platform ±Inf is effectively always invalid, so the new behavior is arguably better — but the boundary is inconsistent.
Suggested fix: none required; optionally normalize the k ≤ 5 mask to `isfinite` as well (behavior change — separate decision).

### P3-4 — Doc §14 does not mention the velocity anchor
`docs/processing/sar-domain.md:427` still reads "… producing per-epoch displacement (relative to the reference epoch), OLS linear velocity, and the fit RMS residual". The anchored fit (reference epoch included as the (t0, 0) data point) is documented only in the kernel comment and tests. One clause — "OLS linear velocity anchored on the reference epoch's zero displacement" — would keep the authority file exact.

---

## Verdict

**APPROVE_WITH_FIXES** — recommended before merge:
1. P2-1: make the k>5 median streamable (or cancellable/progressing) instead of full-frame O(w·h·k²).
2. P2-2: typed/detailed errors for the new speckle companion refusals (grid mismatch, band shortage).

The P3 items are optional polish. All nine claimed scientific fixes are mathematically correct, well-tested with independent oracles, and introduce no regressions in the 11 related existing suites re-run.
