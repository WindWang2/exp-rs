# REVIEW_R2 — r23-A science-algorithms fix track (independent reviewer R2, adversarial re-check)

- Reviewer: R2 (independent; did not author any commit on this branch; derived every conclusion
  from the code and executed evidence below, not from REVIEW_R1)
- Date: 2026-10-27
- Worktree: `exp-rs-r23-A-20261026T164112Z`, branch `agent/r23-a-science-algorithms-20261026T164112Z`
- Scope: full `git diff origin/master...HEAD` hunk-by-hunk (10 algorithm fixes, 12 test files,
  planning docs), PLUS an explicit adversarial audit of the R1-closure commit `717fc811c0` for
  problems NEW in the R1 round.
- Tree state: `git status --porcelain` empty at start, after each mutation restore, and at the
  end of review (verified 3×).

## 1. Method

Independent re-derivation of every numerical change against the actual sources (master forms
recovered via `git show origin/master:...`); grep of all callers of every changed symbol;
2 in-place mutation checks (revert → rebuild `-j2` under `flock $HOME/.cache/exp-rs-build.lock`
→ RED → restore → rebuild → GREEN); 3 standalone harnesses compiled directly against HEAD
sources (`g++ -std=c++20 -I src -I src/processing`): an 18-grid adversarial viewshed ring
harness, a polar band-edge harness, and a #1687 seam-semantics harness; 15 test binaries run
(12 required + 3 neighbors), re-run once more after the final mutation restore; planning docs
cross-checked against measured values.

## 2. Fix-by-fix independent verification

**F1 math_utils #1687 + legacyNegativeSentinelFree seam.**
Master's `bothNormalizable` = finite AND `> -999` both AND NOT(equal negatives). HEAD splits it:
`bothNormalizable` = finite-only (math_utils.cpp:180); `legacyNegativeSentinelFree` = that AND
`a > -999 && b > -999` (math_utils.cpp:193-203). Re-derived: the seam predicate excludes
exactly `<= -999` (strict `>` at the boundary, −999.0 voided, −998.999 computed — executed,
§5 harness C) — identical boundary to master's `a <= -999.0f`, minus the equal-negative rule,
which is the intended #1687 change (a==b==−0.01 → ND = −0.0, well-defined; safeDiv(0,−0.02)).
Zero-sum pairs still NaN via safeDiv (math_utils.cpp:12-15: `denominator == 0.0f` covers ±0).
Callers re-grepped independently: `normalizedDifference` → change_detection.cpp:49,
spectral_indices.cpp (7), rs_spectral_index_operator.cpp (11 sites), rs_temporal_index_operator.cpp:97;
`bothNormalizable` → only the kernel itself; `legacyNegativeSentinelFree` → only the dNBR seam
(rs_spectral_index_operator.cpp:786-787). Signature/ABI unchanged. **PASS**, with the
cross-branch exposure finding P2-1 below.

**F2 viewshed ring quantum m/2** — see §4 (dedicated adversarial duty). **PASS** (invariant
empirically unbroken on 18 adversarial grids, 17,280 cells; two P3 notes: realized drop can be
exactly 1 under ceil rounding; int-cast UB window widened).

**F3 curvature stencil.** Layout independently confirmed `k9[(dy+1)*3+(dx+1)]`
(terrain_analysis.cpp:631-638). Mixed partial = x-difference of y-slopes:
[(i−c)−(g−a)]/(4·csx·csy) = **(a+i−c−g)/(4 csx csy)** — the new code. The old (g+i−a−c) equals
bottom-row-sum minus top-row-sum: symmetric under x→−x flip, cannot be a mixed partial (a mixed
partial must be odd under each single-axis flip). Consumers: profile/plan change values
(intended correction); curvatureTotal uses zxx+zyy only → invariant. Anisotropic algebra
re-derived: z=x·y, csx=0.5, csy=2 at (2,2): zx=2/0.5=4, zy=2/2=1, zxy=1/(0.5·2)=1 →
profile = 2·4·1·1/17 = 8/17 ✓ (matches committed test). **PASS.**

**F4 ROI wavelength units.** Routes through `SpectralWavelength::normalizeToNm` →
`normalizePair` (spectral_wavelength.cpp:41-59): empty units → nm as-is; µm family → ×1000;
unsupported → UnknownUnits → false → 0.0f "absent" (same as unparsable). Consistent with the
single-authority claim; old code stored raw µm (0.49 for 490 nm). **PASS.**

**F5 streaming stretch parity.** New mask `in[i] == nodata || !std::isfinite(in[i])`
(image_enhancement_streaming.cpp:700) and the scaling formula are byte-identical to the
full-frame `ImageEnhancement::linearStretch` (image_enhancement.cpp:33-36) — verified by
side-by-side read; parity additionally pinned element-wise by the new test. **PASS.**

**F6 polar azimuth band.** Limit independently re-derived: near φ=90°−ε',
sin(elev)=sinφ·sinδ+cosφ·cosδ·cosH ⇒ numerator = cosφ·(sinδ·cosφ − sinφ·cosδ·cosH) →
cos(az) = −cosδ·cosH/cos(elev) → **−cos H** (north) / **+cos H** (south) — matches
`(lat >= 0 ? -1 : 1) * cos(hourAngle)`. Convention consistent with the acos branch at
equinox/mid-latitude (H=−90°, δ=0 ⇒ az=90° east everywhere, including the limit); afternoon
flip 2π−az applied after the branch ⇒ west at 270° for H=+90°. Threshold analysis in §4c.
**PASS.**

**F7 harmonic clamp.** Only 3 references to `harmonics` remain in temporal_fit.cpp (param +
comment); both design-row fills loop `k <= h`, `terms = 1+2h`, `h = clamp(harmonics,1,6)` at
entry. design[13] holds indices 0..12 exactly for h=6. Old form: harmonics≥7 wrote design[13..14]
(stack OOB); harmonics≤0 left design[1..2] uninitialized while terms=3. Operator clamps before
calling (verified in rs_temporal_harmonic_fit_operator.cpp). **PASS.**

**F8 MNF empty selection.** `inverse()` (mnf_transform.cpp:434-447) treats empty components as
"keep all"; `reconstructionRmse` now returns 0.0 for empty (mnf_transform.cpp:466-468) — the
subsets agree. Also verified mask symmetry for weird selections: duplicates idempotent;
out-of-range indices skipped by BOTH functions (inverse:449-451, rmse:475-476), so the two
cannot disagree (e.g. {99} = "drop nothing valid selected" → mean-only reconstruction and
full-mass RMSE, consistently on both sides). **PASS.**

**F9 VCA scale-free gate.** Re-derived the refusal corners independently
(spectral_unmixing.cpp:665-673):
- all-identical cube: every dir = +0.0 → norm² = 0.0 → `bestNorm > 0.0` false → **refuses** ✓
- collinear cube, endmemberCount 3: pick 2 residual after projecting out the line direction is
  FP noise ≈ (2.2e-16·L)² = 4.9e-32·L² versus gate 1e-12·anchorNorm2 (~L²) — a ~20-decade
  margin, **refuses** ✓ and cannot falsely pass;
- genuine simplex at scale s: residual²/anchor² is scale-invariant, passes for any s where the
  geometry constant exceeds 1e-12 (the 1e-7-radiance committed case: ratio O(1)) ✓;
- NaN pixels never selected (NaN > x false); all-NaN → haveCandidate false → refuses; +Inf
  pixel → anchor=Inf → later gate 1e-12·Inf fails closed, no UB ✓.
Scale-free-ness is only on the pick gate; the GS-direction gate stays absolute 1e-12
(spectral_unmixing.cpp:695) — acceptance strictly widened vs master (old absolute pick gate
1e-12 refused everything below ~1e-6 scale); R1's P3-2 stands, acknowledged in RISK_REGISTER.
**PASS.**

**F10 spectral_table width refusal.** digestHex (spectral_table.cpp:41-50) and toJson
(:172-181) pre-scan `row.size() != bandCount` and return empty QString/QJsonObject before the
cell walk — no OOB read. Width-refusal now logs via qWarning (R1 closure, §6). **PASS.**

**spectral_anomaly.h doc commit.** Diff is comment-only (12 changed lines, all in `///` docs);
the .cpp is untouched by the branch (empty diff stat). Independently verified the docs now
match the code: finalizeCovariance uses N−1 for count>1 (spectral_anomaly.cpp:117-119),
invertCovariance ridge = max(1e-9, (trace/bands)·1e-7) (:141). Fixes a stale master comment.
**PASS.**

## 3. Special duty 2(a): the dNBR seam and the OTHER branches

`readTileBand` (rs_spectral_index_operator.cpp:477-510) resolves only the DECLARED band
sentinel (`src.hasNodata`) and non-finite values to NaN; a finite undeclared −9999 flows into
the kernels. The seam screen (`legacyNegativeSentinelFree`, :786-787) voids the −9999 family
on dNBR only. **Every other normalized-difference branch of the same operator — NDVI, NDWI,
NDBI, MNDWI, NBR, NDRE, NDSI, NDTI, GNDVI, NDMI, UI — reads the same external raster through
the same readTileBand and now computes where master NaN'd.** Executed (§5, section B):
`normalizedDifference(−9999, 0.3)` at HEAD = **+1.0000600** (exact: (−9999.6)/(−9998.7));
NBR with an undeclared −32768 = +1.0000122. On master both were NaN. Same exposure class in
rs_temporal_index_operator.cpp:97, spectral_indices.cpp, change_detection.cpp (all
sentinel-blind by #1687 architecture). This is the deliberate #1687 design ("declared NoData
is the caller's authority") and it buys the real win of section A (valid negative
reflectances), but the r21-A M3 fabrication mode is now screened on exactly one of 12 operator
branches. → **P2-1**.

## 4. Special duty 2(b): viewshed m/2 quantum under floating point

Re-derivation: toward-observer step lowers d by ≥ csx|ux|+csy|uy| ≥ m·(|ux|+|uy|) = m
(u = unit radial; exact for any anisotropy since the projection bound is Cauchy-Schwarz).
Quotient drop ≥ 2 in EXACT arithmetic; the sweep only needs ≥ 1.

Harness (standalone g++ against HEAD terrain_viewshed.cpp, no reimplementation of the sweep):
18 adversarial grids — isotropic 0.3/0.5/1/30 m; anisotropic 0.25/0.8, 3/7, 1e3/1e-3,
1e5/1e-2, 1/1e-3, 1/1e-6 (ratio 1e6) both orientations; powers of two (0.125/0.5);
FP-adjacent pair 0.3 vs 0.3·(1+1e-16); sub-mm 1e-4. **17,280 cells checked: zero cells
without a strictly-smaller-ring in-grid predecessor; flat-plane viewshedR3 returned
visible=961/961 (FULL) on every grid.** Ring quotients up to 3e8 (1/7 of INT_MAX) exercised.

- **No FP counterexample to the invariant exists short of int-overflow land**: breaking the
  margin-2 needs quotient perturbation ≥ O(1), i.e. relative FP error ~1e-16 only at quotient
  ~1e16 ≫ INT_MAX — the `static_cast<int>` UB triggers long before rounding can.
- Nuance found (P3-2): the comment's "drop by >= 2 … survives double rounding" holds as an
  exact-arithmetic statement; empirically 30 of 17,280 cells realize a drop of exactly 1
  (one-ulp ceil-boundary rounding of the two quotients, the very mechanism the ledger
  documented for the first iteration). The invariant needs only ≥ 1 and always has it — code
  correct, comment one notch optimistic.
- P3-3: the int-cast UB window is wider than master's: `ceil(d/ringStep) > INT_MAX` at
  2d/min(csx,csy) > 2.147e9 (master needed d > 2^31 m). Constructed: csx=5e8, csy=0.9,
  2 columns from the observer → d=1e9 (master safe) but quotient 2.222e9 > INT_MAX (HEAD UB).
  Unreachable for physical data (0.3 m DSM ⇒ threshold at d ≈ 3.2e8 m); pre-existing UB class,
  threshold lowered. Recommend a cheap double-range guard in a future pass.

## 5. Executed counterexamples (all against HEAD sources)

### 5a. #1687 seam semantics — math_utils.cpp harness (Qt-free standalone)

| case | kernel (NDVI/NBR branches) | dNBR seam | master |
|---|---|---|---|
| A. NIR=Red=−0.01 (dark water) | **ND = −0.0000000** | accept, ND = −0.0 | NaN (fabricated NoData) |
| A. NIR=−0.02, Red=−0.01 | **ND = +0.3333333** | accept | NaN |
| B. NDVI, undeclared NIR=−9999, Red=0.3 | **ND = +1.0000600** | void → NaN | NaN |
| B. NBR, undeclared NIR=−32768, SWIR2=0.2 | **ND = +1.0000122** | void → NaN | NaN |
| C. seam boundary −999.0 | — | void (exactly ≤−999) | void |
| C. seam boundary −998.999 | — | computed (+1.0006008) | computed |
| D. fused dNBR pre(−0.01,−0.01) post(0.30,0.50) | — | **dNBR = +0.2500** | NaN |

Arithmetic for B: (−9999 − 0.3)/(−9999 + 0.3) = −10002.3/−9998.7 = **+1.0000600** — a
physically impossible "perfect vegetation" NDVI fabricated from a legacy sentinel; exactly the
M3 failure mode, now reachable on the 11 non-dNBR branches (P2-1). Arithmetic for D:
NBR_pre = (−0.01−(−0.01))/(−0.01+(−0.01)) = 0/−0.02 = −0.0; NBR_post = (0.30−0.50)/0.80 =
−0.25; dNBR = −0.0 − (−0.25) = **+0.25** (master: NaN — the #1687 win, end-to-end at the seam).

### 5b. Polar band edge — terrain_solar.cpp harness (task 2c)

Committed tests pin the poles exactly (harness reproduced az = 180.000000 / 0.000000 /
270.000000°, elev = ±decl = ∓23.452°, i.e. elevation ≡ declination at the pole). The band edge
itself: straddling |cos lat| = 1e-12 (0.9× vs 1.1× edge width; the band is 5.73e-11° ≈ 4000
doubles wide near 90°), scanning all 24 h at 0.002 h steps, days 80/172/300: **max azimuth
discontinuity between the two branches = 0.28°** (day 172, t≈12.03 h), 0.03–0.28° across days;
exceeds neither the tests' ±2° nor the header's ≲1.5° claim (which is conservative). My
a-priori bound: δcosAz ≈ 3e-16/(|cosφ|·cos elev) ≈ 3e-4 at the edge, δaz ≈ δcosAz/|sin H|,
worst near solar noon where sin H → 0 — measured worst case confirms the bound's scale.
**Threshold 1e-12 validated: the branch swap is invisible at its own edge.** (Doc nit only:
the ≲1.5° figure is unbounded within ~0.5° of exact noon in principle, but the measured worst
case is 5× below it.)

## 6. R1-round audit (commit 717fc811c0) — new problems?

- **qWarning format strings** (spectral_table.cpp:44-46, 175-177): `row.size()` is
  `size_t` → `%zu` ✓; `bandCount` is the `int` parameter → `%d` ✓ (digestHex);
  `table.bandCount` is `int` (spectral_table.h:73) → `%d` ✓ (toJson). Varargs promotion:
  size_t and int passed exactly; **no UB**; compiles clean under GCC's format checking.
  Message wording ("row %zu of %d bands") reads slightly odd but is unambiguous.
- **New dNBR operator test** (test_spectral_formula_drift.cpp:341-397): assumptions verified —
  `writeMultiBandConst` creates the GTiff with no nodata tag (genuinely undeclared); postNir/
  postSwir2 default to nirBand/swir2Band (operator:748-749); readTileBand passes the finite
  −9999 through; the seam voids ⇒ NaN output asserted on all 4 pixels. The test is the call
  site's only regression net — **verified real** by my own seam-revert mutation (§7 M2).
  Nit (P3-1): the test comment says the un-screened path "computes a ~-1 pre-NBR and
  fabricates extreme change (~-1.5 dNBR)" — the actual fabricated values are **+1.00012 /
  +1.4738** (sign error in the comment only; observed in my RED run).
- **terrain_solar.h header doc** — added sentence matches the code (band definition, convention,
  ≲1.5° claim conservative per §5b). No new problem.
- **REVIEW_R1.md claims vs code** — spot-checks all reproduced: callers grep (F1) ✓ (independently
  re-grepped, same 20 sites); F6 limit derivation ✓ (re-derived, §2); F8 inverse contract ✓
  (read mnf_transform.cpp:434-447); F9 refusal corners ✓ (re-derived, §2); F7 "operator clamps
  before calling" ✓; F10/`fromJson` width enforcement consistent with the code read; commit-count
  discrepancy (15 vs 13) is bookkeeping ✓. R1's F2 memory-bound arithmetic ("~7e8 cells per
  side") corresponds to the corner-observer worst case (d/m ≈ √2·N) — order correct.
  R1 §3's viewshed RED narrative attributes "75 of 441" to the 0.3 m grid; my mutation shows
  the 0.3/0.3 section fails with **9** of 441 and the 0.25/0.8 section with **75** (attribution
  swapped; both values genuine — P3-4).
- **RISK_REGISTER R1 dispositions** — each verified against HEAD: P2-1 fixed (test added,
  mutation-verified), P3-3 fixed (qWarning), P3-4 fixed (header), P3-2 accepted+registered,
  P3-1/P3-5/P3-6 deferred to PR prep. Accurate.

## 7. Mutation checks (2 — one of my choosing + re-verification of the R1-closure claim)

| # | Fix reverted in place | Target rebuilt | RED observed | Restored GREEN |
|---|---|---|---|---|
| M1 (mine) | terrain_viewshed.cpp:104 `ceil(d/ringStep)` → master `ceil(d)` | test_terrain_viewshed | **exit 42**, 2 failed CHECKs: `9 == 441` (0.3/0.3) and `75 == 441` (0.25/0.8); 1455/1457 passed | exit 0, 1457/8 |
| M2 (R1-claim re-check) | rs_spectral_index_operator.cpp:786-787 seam → `bothNormalizable` | test_spectral_formula_drift (+libsicnu_operators) | **exit 42**, `REQUIRE(std::isnan(value))` fails, "dNBR pixel **1.4738** (undeclared -9999 pre-NIR)" | exit 0, 155/5 |

Both mutations restored, rebuilt, GREEN, `git status --porcelain` empty after each. The R1
commit-message claim "seam revert -> RED exit 42, restore -> 155 assertions green" is genuine.
M1 closes the gap R1 left as "reasoned RED only" — the viewshed mutation RED is real and
reproduces TEST_RESULTS.md's values (with the 9/75 attribution nuance above).

## 8. Test runs (QT_QPA_PLATFORM=offscreen, build-r23-A/tests; run twice — before mutations
and after final restore; identical counts both times)

Required 12 — all exit=0:

| binary | assertions | cases |
|---|---|---|
| test_math_utils | 109 | 8 |
| test_terrain_viewshed | 1457 | 8 |
| test_terrain_foundation5 | 277 | 14 |
| test_spectral_roi | 43 | 3 |
| test_terrain_solar | 80 | 6 |
| test_temporal_fit | 212 | 27 |
| test_mnf_transform | 1883 | 8 |
| test_spectral_unmixing_fcls | 100 | 8 |
| test_spectral_table | 64 | 10 |
| test_image_enhancement | 6247 | 16 |
| test_image_fusion | 1727 | 49 |
| test_spectral_formula_drift | 155 | 5 |

Neighbors (my choice: the #1687 seam consumers and the operator surface) — all exit=0:
test_change_detection 888684/41 · test_band_math 130277/49 · test_rs_operators 9114/84.

Counts match TEST_RESULTS.md's GREEN table exactly (drift is 155/5 on HEAD vs 145/4 in the
pre-R1-closure table — the +10 assertions are the new sentinel case; the neighbor table's 145
predates 717fc811c0). Build state verified current before running (all .so newer than every
touched source; no-op `make` confirmed).

## 9. Findings

**[P2-1] (R2) Undeclared-sentinel fabrication is now reachable on every non-dNBR index branch —
the protection is asymmetric inside one operator and nowhere disclosed.**
Files: src/operators/rs/rs_spectral_index_operator.cpp:655-926 (11 normalized-difference
branches read external rasters through readTileBand, which resolves only DECLARED NoData);
src/operators/rs/rs_temporal_index_operator.cpp:97; src/processing/algorithms/spectral_indices.cpp;
src/processing/algorithms/change_detection.cpp:49.
Evidence (executed, §5a): NDVI on an undeclared −9999 pixel computes +1.0000600 at HEAD;
master NaN'd. The r21-A R2/M3 failure mode this track exists to prevent is screened only at
the dNBR seam; a user running rs:spectral_index NDVI/NBR/NDMI/... on the same legacy raster
gets fabricated near-±1 index values with no warning. This is the explicit #1687 kernel
contract (declared NoData is the authority; the old magic threshold destroyed valid negative
reflectances — also real, §5a A), so the fix itself is not wrong; the gap is that the operator
applies the legacy accommodation on 1 of 12 branches and neither tests, operator help, nor the
R1 round disclose the other 11 (REVIEW_R1 P3-6 covers only the staged-vs-fused dNBR
divergence).
Recommendation (choose one, before or in the PR): (a) apply `legacyNegativeSentinelFree` at
all external-band pair reads in runSpectralIndexCore for the ND-family branches (operator-level
screen, kernels stay sentinel-blind), or (b) add a dataset-level undeclared-sentinel probe
mirroring the numeric-domain probe (#801) with a logWarning, or (c) at minimum disclose the
branch asymmetry in the PR body. Not blocking: deliberate architecture decision, no test
regresses, declared-NoData rasters (the supported path) are unaffected.

**[P3-1] (R2, R1-round) Sign error in the new dNBR test's comment.**
tests/test_spectral_formula_drift.cpp:256-258: "computes a ~-1 pre-NBR and fabricates extreme
change (~-1.5 dNBR)" — actual fabricated pre-NBR is +1.00012 and dNBR +1.4738 (observed in
mutation M2). Cosmetic; correct the signs so future readers trust the RED narrative.

**[P3-2] (R2) Viewshed comment's margin is exact-arithmetic only.**
terrain_viewshed.cpp:66-76: "quantising by m/2 makes the ring quotient drop by >= 2 … survives
double rounding". Measured: 30 of 17,280 cells realize a drop of exactly 1 (one-ulp
ceil-boundary rounding on exact-integer quotients); invariant (drop ≥ 1) never broken. Code
correct; suggest "(exact ≥ 2; ≥ 1 after ceil rounding — which is all the sweep needs)".

**[P3-3] (R2) Ring int-cast UB window widened by the quantum change.**
terrain_viewshed.cpp:104: `static_cast<int>(std::ceil(d / ringStep))` is UB when
2d/min(csx,csy) > INT_MAX; master required d > 2^31 map units. Constructed (not run):
csx=5e8, csy=0.9, 2 columns → quotient 2.222e9 > INT_MAX with d=1e9 (master safe). Needs
csx/csy ≳ 5.6e8 or d/cell > 1.07e9 — physically absurd (0.3 m DSM ⇒ 3.2e8 m extent), same UB
class as master. Recommend a `r > INT_MAX` double guard in a future hardening pass.

**[P3-4] (R2) Planning-doc precision nits.**
TEST_RESULTS.md RED table cites only "75 == 441" for viewshed (actual: 9 on 0.3/0.3, 75 on
0.25/0.8; R1 §3 attributes 75 to the 0.3 m grid — swapped); TEST_RESULTS.md neighbor table
still lists test_spectral_formula_drift 145 (pre-closure; HEAD 155/5); ORACLES.md O3 still
presents the first-iteration whole-cell-quantum argument ("前驱环 ≤ 当前环−1 … 永不断链") as
the fix rationale without the FP ceil-boundary caveat that LOOP_LEDGER R4 documents and that
motivated the final m/2 form. All cosmetic; no false claims about HEAD behavior.

**[P3-5] (carried, unchanged) TASKS.md T11–T14 PENDING / LOOP_LEDGER stops at R5.1** — already
acknowledged in RISK_REGISTER as pre-PR cleanup; 717fc811c0 did not touch them.

## 10. What I verified as NOT broken (R1-round new-problem sweep)

qWarning varargs types (no UB, §6); the new operator test's raster/nodata assumptions (§6);
the header polar-band doc vs code (§5b); spectral_anomaly docs vs unchanged code (§2);
tests/CMakeLists.txt untouched (41-file diff contains no CMake file — append-only discipline
holds); revert-pairing note (R1 P3-1) consistent with my caller analysis; no caller of the
changed predicates was missed (independent grep, §2 F1).

## Verdict

**APPROVE** — with P2-1 required as PR-description disclosure (or the small operator-level
screen) rather than as a merge blocker.

All ten algorithm fixes are independently re-derived correct; the two R1 closures are genuine
(verified by mutation and side-by-side reads); 15/15 test binaries green twice with counts
matching the documented tables; both mutations reproduce RED exactly and restore GREEN with a
clean tree; no P0/P1; the single P2 is a disclosed-architecture residual, not a defect in what
this track set out to fix.
