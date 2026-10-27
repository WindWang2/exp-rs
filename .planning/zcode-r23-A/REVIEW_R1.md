# REVIEW_R1 — r23-A science-algorithms fix track (independent reviewer R1)

- Reviewer: R1 (independent; did not author any of these commits)
- Date: 2026-10-27
- Worktree: `exp-rs-r23-A-20261026T164112Z`, branch `agent/r23-a-science-algorithms-20261026T164112Z`
- Scope: `git diff origin/master...HEAD`, 15 commits (task brief said 13; actual = 1 planning
  + 10 algorithm fixes + 1 contract-doc commit + 1 dNBR seam follow-up + 2 later planning commits
  — discrepancy is bookkeeping only, everything reviewed)
- Tree state at start and end of review: `git status --porcelain` empty (verified; all mutation
  reverts restored and rebuilt GREEN before writing this file)

## 1. Method

Full hunk-by-hunk read of the diff; independent re-derivation of every numerical change;
grep of all callers of every changed function; 4 in-place mutation checks (revert → rebuild →
RED → restore → rebuild → GREEN, all under `CMAKE_BUILD_PARALLEL_LEVEL=2`,
`QT_QPA_PLATFORM=offscreen`, `flock $HOME/.cache/exp-rs-build.lock`); 2 independent numeric
counterexamples executed against the actual built `libsicnu_processing.so` via a /tmp harness;
24 test binaries run (12 required + 12 neighbor suites); planning docs cross-checked against
measured outputs.

## 2. Fix-by-fix verification

### F1 math_utils #1687 (cc34cb4ed6) — PASS
- `bothNormalizable` = finite-only; `normalizedDifference` NaN only for non-finite operands or
  zero sum (safeDiv contract, math_utils.cpp:214 + safeDiv at :505). Old rules (≤−999 magic,
  equal-negative pair) removed per the documented NoData-authority architecture. New
  `legacyNegativeSentinelFree` (math_utils.cpp:193, added in d4f284c6b4) = finite AND
  `> -999` both operands — exactly the old screen minus the equal-negative rule.
- Callers grepped: `normalizedDifference` → change_detection.cpp:49, spectral_indices.cpp
  (7 sites), rs_spectral_index_operator.cpp (10 sites), rs_temporal_index_operator.cpp:97.
  `bothNormalizable` → only math_utils.cpp:214 + the dNBR seam. Behavior change for
  finite ≤−999 inputs is the intended fix; all production callers pre-resolve DECLARED NoData
  to NaN (verified claim, see RISK R1).
- Boundary: NaN/±Inf → NaN; (x,−x) → NaN via safeDiv; count==0 → false. (0,0) → 0/0 → NaN.
  Note: a=3e38, b=−2e38 gives finite sum, Inf numerator → Inf out (pre-existing safeDiv
  semantics, unchanged by this diff).
- ABI: signatures unchanged; pure link-compat.

### F2 viewshed ring quantization (b6b500d86f) — PASS
- Re-derived the invariant myself: toward-observer 8-neighbour step s = (−sign ux·csx,
  −sign uy·csy) lowers d by ≥ csx|ux| + csy|uy| ≥ min(csx,csy)·(|ux|+|uy|) ≥ m. With
  quantum m/2, ring' = ceil(2d'/m) ≤ ceil(2d/m − 2) = ring − 2. Margin of 2 survives ±1-ulp
  ceil/rounding discrepancies on exact-integer quotients (the failure mode of the first
  iteration, correctly diagnosed in the ledger). NoData/NaN → kRingInvalid, skipped at the
  `ring[p] < r && ring[p] != kRingInvalid` guard (terrain_viewshed.cpp:153).
- Memory/perf bound (task item c): per-cell arrays (ring/za/distMap/horizonMax/visible ≈
  17 B/cell) dominate; ring-index structures are 16·maxRing B with maxRing = 2·(old) =
  2·diagonal/m. For any grid whose DEM fits in RAM (≤ ~1e9 cells), maxRing ≤ ~1e6 → a few MB;
  to overflow `int` ring or make ringStart dominate you would need ≥ ~7e8 cells per side
  (≥ 5e16 cells total) — physically impossible. Complexity unchanged O(8n).
- Result equivalence for previously-working grids: ring grouping is coarser but dependencies
  remain strictly ring-decreasing and rings are processed in increasing order, so outputs are
  unchanged on integer-metre grids — verified green by the 1457 pre-existing viewshed assertions.

### F3 curvature stencil (de54a4f17f) — PASS
- Layout confirmed row-major k9[(dy+1)*3+(dx+1)] (terrain_analysis.cpp:695). Mixed partial =
  x-difference of y-slopes: {[(i−c) − (g−a)]}/(4csx·csy) = (a+i−c−g)/(4 csx csy) — the new
  code. Old (g+i−a−c) is the y-second-difference of corner row-sums, x-flip symmetric,
  cannot be a mixed partial. Header formula comment updated in the same commit. Consumers:
  curvatureProfile (:643), curvaturePlan (:674) change values (intended — wrong values
  corrected); curvatureTotal (:701) uses zxx+zyy only, invariant.
- Independent counterexample executed, see §4.

### F4 ROI wavelength units (be5936c3af) — PASS
- Routes through `SpectralWavelength::normalizeToNm` (single authority, spectral_wavelength.cpp:81):
  empty units → nm as-is; µm family → ×1000; unsupported → false → 0 ("absent"), matching the
  documented choice. Consistent with rs_spectral_band_select_operator.cpp:285. Old code stored
  raw µm (0.49 for 490 nm). Caller rs_roi_spectrum_tool.cpp:170 unaffected signature-wise.

### F5 streaming stretch parity (bf02bbd194) — PASS
- `!std::isfinite` now matches full-frame `linearStretch` mask; ±Inf → nodata instead of a
  saturated valid value. Parity pinned element-wise by the new test.

### F6 polar azimuth (507abf0f75) — PASS
- Re-derived the limit: at lat = 90°−ε, numerator = −ε·cosδ·cosH + O(ε²), denominator =
  cos(δ)·ε, so cos(az) → −cos(H); at the south pole → +cos(H). Code
  `(lat >= 0 ? -1 : 1) * cos(hourAngle)` matches. Afternoon flip applied consistently
  (H=+90° north → 90° → 270° west). Branch guard |cosLat| < 1e-12 ≈ |lat−90°| < 6e-11° —
  only the exact-pole degenerate band; away from it the acos form's numerator signal
  (~|cosLat|·cosδ) dominates its cancellation noise (~1e-16) down to |cosLat| ~ 1e-12 with
  worst-case azimuth error ≲ ~1.5° at the band edge in the acos-near-±1 geometry — acceptable
  for a degenerate latitude. See P3-4 for the header-doc gap.

### F7 harmonic clamp (c8cc2b55db) — PASS
- Single clamp `h` used by `terms` AND both design-row fills (both sites verified in full
  function read; no third site; `harmonics` no longer referenced raw). design[13] exactly
  holds max indices 2·6=12. Old harmonics≥7 wrote design[13..14] (stack OOB — their RED run
  caught SIGABRT via stack-protector, plausible and documented); harmonics≤0 left
  design[1..2] uninitialized. Operator clamps before calling (rs_temporal_harmonic_fit_operator.cpp:173
  `std::clamp(getInt(...), 1, 6)`) — the test comment's claim verified true.

### F8 MNF empty selection (4f5da7d59d) — PASS
- Mask semantics: 1 = dropped; empty `components` used to drop everything (max RMSE) while
  `inverse()` with empty = keep all (mnf_transform.cpp:435-447) — the contradiction. Now
  returns 0.0, exact in exact arithmetic (full-basis reconstruction is the identity up to
  fp orthogonality noise ~1e-15). Callers rs_mnf_inverse_operator.cpp:252/354 pass through —
  empty-components operator output changes from "large error" to 0, which is the correct
  contract.

### F9 VCA relative gate (a026e901b6) — PASS
- k==0: gate `bestNorm > 0.0` (all pixels coincide with mean → honest false); anchors
  `anchorNorm2`. k≥1: `bestNorm > 1e-12·anchorNorm2` = residual length > 1e-6·anchor length,
  ~5000× above the ε·anchorNorm2 cancellation noise floor of the projection — sound.
  NaN pixels never selected (NaN `>` is false); all-NaN cube → haveCandidate false → false;
  Inf pixel → anchor Inf → later picks fail closed (refusal, no UB). Extra bookkeeping: one
  double + one compare per call — negligible. Residual absolutism in the GS gate, see P3-2.

### F10 spectral_table width guard (04552c8a05) — PASS
- Both `digestHex` and `toJson` pre-scan rows for `row.size() != bandCount` and return empty
  ("" / {}) — no OOB walk of row[c]. fromJson-built tables can't be width-inconsistent, so
  this is defense-in-depth for programmatic construction; empty-string digest is uniquely
  distinguishable from any valid digest (valid tables always hash non-empty). Silent at the
  operator seam — see P3-3.

### dNBR seam follow-up (d4f284c6b4) — PASS with a test-gap finding (P2-1)
- Seam scoped to the fused dNBR branch only (rs_spectral_index_operator.cpp:786-787); all
  other operator branches use the sentinel-blind shared kernel, consistent with #1687.
- `legacyNegativeSentinelFree` restores the undeclared −9999 screen at exactly the seam the
  r21-A M3 regression demanded. Note the seam does NOT restore the old equal-negative rule
  (`a==b<0`): pairs like (−0.01, −0.01) now compute NDVI 0 at the seam — deliberate per #1687
  semantics, documented in the fusion test.

### Contract-doc commit (f9c5eca270) — PASS
- Comment-only. Verified against the UNCHANGED spectral_anomaly.cpp: finalizeCovariance uses
  N−1 for count>1 (line 119), invertCovariance ridge = max(1e-9, (trace/bands)·1e-7)
  (line 141), and rxDetector literally shares those helpers, so "same N−1 estimator" is
  accurate. The docs fix a stale comment/code mismatch; no behavior change.

## 3. Mutation checks (4 in-place reverts, all RED observed, then restored GREEN)

| # | Fix reverted (in place) | Target rebuilt | RED observed | Restored GREEN |
|---|---|---|---|---|
| 1 | curvature stencil → `(g+i−a−c)` | test_terrain_foundation5 | `REQUIRE(prof[centre] == Approx(1.0))` → `2.0f == Approx(1.0)`, exit 42 | exit 0, 10/10 assertions |
| 2 | terrain_solar.cpp → master | test_terrain_solar | `171.18842006804209177 == Approx(180.0)` and `8.81157993195791711 == Approx(0.0)`, exit 42 (2 of 11 failed) | exit 0, 80 assertions |
| 3 | mnf_transform.cpp → master | test_mnf_transform | `0.11739621312180989 == Approx(0.0)` (2 of 4 failed), exit 42 | exit 0, 1883 assertions |
| 4 | math_utils.{h,cpp} → master | test_math_utils | 3 of 22 failed (identical failure set to the documented RED batch), exit 42 | exit 0, 109 assertions; test_image_fusion 1727 exit 0 |

Reasoned RED verification for the remaining fixes (not reverted in place):
- viewshed: on the 0.3 m grid the old `ceil(d)` puts axial cells d=0.3/0.6/0.9 all in ring 1;
  cell (10,12) (d=0.6) has no strictly-smaller-ring in-grid neighbour (observer ring 0 is not
  adjacent) → dark cells → `visible == vis.size()` fails (docs report 75 of 441 visible).
- temporal: harmonics=0 → terms=3 but design[1..2] never filled → `zero ≈ one` coefficient
  comparison fails (uninitialized stack); harmonics=7 → design[13..14] OOB (documented
  SIGABRT under stack-protector).
- ROI: old code returns 0.49/0.665 → `Approx(490.0)` fails.
- table: old digestHex hashes OOB row[2] → non-empty digest → `isEmpty()` fails.
- enhancement: old ±Inf → clamped 255/0 ≠ frame nodata → element-wise parity REQUIRE fails.
- VCA: old absolute gate 1e-12 vs anchor pick norm² ≈ 2.2e-14 → false at k==0 → REQUIRE fails.
Mutation values 2.0 / 171.19 / 8.81 / 0.1174 / 19-of-22 reproduce TEST_RESULTS.md's RED batch
verbatim on every row I could compare — the documented RED evidence is genuine.

## 4. Independent counterexamples (both executed against the built library via /tmp harness)

### 4a. Curvature — z = x²·y (not the committed z=x·y saddle), 5×5, csx=csy=1, centre (2,2)
Stencil values (x,y ∈ 1..4 around centre at (3,3), z=x²y):
a=z(2,2)=8, c=z(4,2)=32, g=z(2,4)=16, i=z(4,4)=64, e=z(3,3)=27, d=z(2,3)=12, f=z(4,3)=48,
b=z(3,2)=18, h=z(3,4)=36.
- Analytic truth at centre: zx=2xy=18, zy=x²=9, zxx=2y=6, zyy=0, zxy=2x=6.
- Central differences confirm: zx=(48−12)/2=18 ✓, zy=(36−18)/2=9 ✓, zxx=(12+48−54)=6 ✓,
  zyy=(18+36−54)=0 ✓.
- NEW stencil: zxy=(8+64−32−16)/4 = 24/4 = 6 ✓ (exactly 2x).
- OLD stencil: zxy=(16+64−8−32)/4 = 40/4 = 10 ✗ (not even a constant multiple of the truth —
  it equals x²+1, contaminated by a y²-of-row-sums term; the z=x·y saddle where old=2·true is
  a special case).
- profile = (zx²·zxx + 2·zx·zy·zxy + zy²·zyy)/(zx²+zy²):
  fixed = (324·6 + 2·18·9·6 + 0)/405 = 3888/405 = 9.6; old = (1944 + 3240)/405 = 12.8.
- Executed: built library returns **9.600000** (hand: 9.6; old stencil would give 12.8).

### 4b. Polar azimuth — south pole, day 172 (June solstice), solarHour 15:00 (H=+45°)
- Polar limit of the shared convention (independent of declination):
  az = 360° − acos(cos 45°) = 360° − 45° = **315°**; elevation = −decl ≈ −23.4° (polar night).
- Executed: built library returns **az=315.000000, elev=−23.452864** (Spencer declination for
  day 172 ≈ −23.45° ✓).
- Old code at lat=−90° divides the cancelled numerator (sinδ − sin(elev)·sin(lat), an O(1)−O(1)
  cancellation leaving ±~1 ulp ≈ 5.5e-17) by cos(lat)·cos(elev) ≈ 5.6e-17 → O(1) rounding
  noise with a success return; the deterministic instance of that noise was captured in
  mutation #2 (171.19°/8.81°). Additional run: north pole, equinox day 80, H=−90° →
  **az=90.000000 exactly** (east), elev=−0.1647° = declination(day 80) ✓ (at the pole
  elevation ≡ declination; sun circles the horizon — physically correct).

## 5. Test runs (QT_QPA_PLATFORM=offscreen, binaries under build-r23-A/tests/)

Required 12 — all exit=0, counts match TEST_RESULTS.md exactly:
test_math_utils 109/8 · test_terrain_viewshed 1457/8 · test_terrain_foundation5 277/14 ·
test_spectral_roi 43/3 · test_terrain_solar 80/6 · test_temporal_fit 212/27 ·
test_mnf_transform 1883/8 · test_spectral_unmixing_fcls 100/8 · test_spectral_table 64/10 ·
test_image_enhancement 6247/16 · test_image_fusion 1727/49 · test_spectral_formula_drift 145/4.

Neighbor spot-checks (dynamically linked to the current libsicnu_processing.so, so they
exercise HEAD code) — all exit=0, counts match TEST_RESULTS.md's neighbor table exactly:
test_change_detection 888684/41 · test_band_math 130277/49 · test_endmember_extraction 72/4 ·
test_mnf 22/3 · test_spectral_anomaly 90243/9 · test_temporal_selection 118/13 ·
test_temporal_uncertainty 75/11 · test_processing_integration 29/5 · test_mosaic 38/10 ·
test_terrain_analytics_e2e 1088/9 · test_e2e_phase2 4107/49 · test_rs_operators 9114/84.

## 6. Planning-doc audit

No false claims found. Every falsifiable number I checked reproduces exactly:
- GREEN1 assertion/case counts: 12/12 identical (§5).
- RED-batch failure values (171.19/8.81, 0.1174, saddle 2.0, math_utils 19-of-22): reproduced
  by my own mutations.
- Neighbor-regression counts: 12/12 identical (§5).
- "operator clamps before calling" (temporal test comment): true (operator line 173).
- spectral_anomaly contract doc matches the unchanged implementation (§2).
- Honest disclosure of the viewshed first-iteration failure (12 dark cells at 0.3 m, FP
  integer-boundary collision) and of the solar RED differing from the audit's NaN prediction.

## 7. Findings

**[P2-1] dNBR seam is not pinned end-to-end — mutation gap on the exact regression this track
existed to prevent.**
File: src/operators/rs/rs_spectral_index_operator.cpp:786-787; tests/test_image_fusion.cpp:1918-1930;
tests/test_spectral_formula_drift.cpp:267-323.
Evidence: reverting the seam to `MathUtils::bothNormalizable` (i.e., losing the undeclared
−9999 screen again — the r21-A R2/M3 regression) leaves every test green: the fusion test
asserts the predicate function itself, and all operator-level dNBR tests use clean data
(no test feeds an undeclared −9999 band value through runSpectralIndexCore and asserts NaN).
I verified the predicate would catch it, but nothing fails if the CALL SITE regresses.
Fix: add one operator-level test (rs:spectral_index, index=dNBR, pre raster with an
undeclared −9999 NIR pixel) asserting the output pixel is NaN. Until then the seam's
protection is convention + comment only.

**[P3-1] Revert-order coupling between cc34cb4ed6 (#1687) and d4f284c6b4 (seam).**
Evidence: reverting either commit alone compiles but leaves tests red in the other's suite
(test_image_fusion's finite-only `bothNormalizable(nd, 0.3f)` assertion vs the restored
sentinel predicate, or the old M3 assertions vs the finite-only kernel). Reverting both
together is clean. All other 9 fix commits + the contract-doc commit are independently
revertible (disjoint file sets, co-located tests).
Fix: note the revert pairing explicitly in the PR description ("revert d4f284c6b4 and
cc34cb4ed6 together").

**[P3-2] VCA gate not scale-free end-to-end.**
File: src/processing/algorithms/spectral_unmixing.cpp:684 (`norm > 1e-12` on the
Gram–Schmidt direction length).
Evidence: the pick gate is now relative to anchorNorm2, but the GS-direction check remains
absolute, so cubes with intrinsic scale below ~1e-12 still fail there. No regression vs
master (old absolute gate failed below ~1e-6); acceptance strictly widened; the 1e-7
radiance use case passes.
Fix (future): make the GS gate relative too (e.g., norm > 1e-6·sqrt(anchorNorm2)).

**[P3-3] spectral_table typed refusal is silent at the operator seam.**
File: src/operators/rs/rs_endmember_analysis_operator.cpp:295 (digest "" stored, no log).
Evidence: the guard returns "" and the operator stores it without complaint; downstream
consumers (artifact pool) safely reject empty digests, and fromJson tables cannot be
width-inconsistent, so the exposure is limited to programmatic construction.
Fix: emit a warning log when digestHex/toJson refuse.

**[P3-4] Polar-band threshold not documented in the header.**
File: src/processing/algorithms/terrain_solar.h (contract section).
Evidence: the |cosLat| < 1e-12 branch is explained only in the .cpp comment; the header
contract ("low-precision Spencer 1971") does not mention the polar band or its convention
(az = acos(∓cos H)). Worst-case azimuth deviation at the band edge is ≲ ~1.5° (acos-near-±1
geometry) — acceptable, but the contract should say so.
Fix: one sentence in terrain_solar.h.

**[P3-5] Planning docs slightly stale relative to HEAD.**
File: .planning/zcode-r23-A/TASKS.md (T11–T14 still PENDING though the contract-doc commit
and both review rounds are underway/done), .planning/zcode-r23-A/LOOP_LEDGER.md (stops at
R5; the seam round and neighbor regression are recorded only in TEST_RESULTS.md).
Fix: update statuses before the PR (cosmetic; no false claims).

**[P3-6] Observation (no action required): staged-vs-fused divergence for UNDECLARED sentinels.**
For an undeclared −9999 band pair, the fused dNBR seam yields NaN while the staged path
(two index runs + difference) yields ≈±1 garbage (normalizedDifference is sentinel-blind).
Within the declared-NoData contract both paths yield NaN, and the divergence is the explicit
#1687 design decision ("NoData authority is the caller"); the dNBR screen is a documented
legacy accommodation. Worth one line in the PR description so reviewers don't read it as an
oversight. Related pre-existing observation (outside this diff, NOT introduced here):
change_detection.cpp:49 calls MathUtils::normalizedDifference(after, before) — an explicit
argument swap whose sign convention (post−pre vs pre−post numerator) deserves a doc line in a
future track; behavior unchanged by this branch.

## 8. Rollback safety summary

9 of 11 code commits are independently revertible (disjoint files, tests co-located).
cc34cb4ed6 + d4f284c6b4 must be reverted as a pair (P3-1). Doc-only commits trivially
revertible. No commit mixes concerns across files owned by other tracks; tests/CMakeLists.txt
was not modified (append-only discipline respected — no changes to it at all on this branch).

## 9. Performance & memory summary

No complexity changes anywhere. viewshed worst case bounded in §2 (ring index structures
≤ a few MB for any RAM-feasible grid; per-cell arrays dominate and are unchanged in size).
VCA: +1 double bookkeeping. ROI: +O(1)/band metadata call. All others: identical work plus a
branch. PERF_BASELINE.md's "no performance claims" stance is consistent with the code.

## Verdict

APPROVE
