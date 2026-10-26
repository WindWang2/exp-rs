# R22-01 science track — Review Round 1 (independent adversarial review)

- Reviewer: independent (no part in writing the changes)
- Worktree: `agent/r22-science-20261025T133250Z` (4 commits on master @ 911e0b17a3533ca0d79c1b68b000d6052c4c3c61)
- Scope reviewed: full diff of `src`, `tests`, `.gitignore` (29 files, +1096/−39), plus execution of the changed kernels and of 24 test binaries (see §"Verified by independent recomputation").

## Verdict: **APPROVE**

No P0 or P1 defects found. One P2 (stale public-header contract text contradicting the new #1686 semantics) and eight P3 nits. All four fixes are algorithmically correct as implemented; all golden values in the new tests were independently recomputed from a from-scratch MT19937 and match; all at-risk test binaries I identified beyond the author's verified set were built and pass.

---

## Findings

### F1 — P2 — stale contract doc in `resampler.h` contradicts the #1686 fix shipped in the same PR
`src/processing/algorithms/resampler.h:39-40` still reads:

```
    /// 2D sub-pixel sample at pixel coordinate (u, v). Coordinates outside
    /// [0, width-1] x [0, height-1] yield noDataValue.
```

`resampler.cpp` (`sampleNearest`, lines 35–38, and `Resampler::interpolate`, lines 124–129) now implements `[-0.5, width-0.5] × [-0.5, height-0.5]`, and `tests/test_resampler.cpp` was re-commented accordingly, but the **public header contract** — the thing API consumers read — still teaches the defective pixel-center boundary this PR exists to remove. This is exactly the class of drift (doc contract ≠ kernel contract) the repo's own comments warn about.
Suggested fix: update the header doc to the raster-edge footprint, e.g. "Coordinates outside the raster-edge footprint `[-0.5, width-0.5] × [-0.5, height-0.5]` yield `noDataValue`; taps are edge-clamped inside it (#1686)."

### F2 — P3 — `portable_random.h` summary line contradicts its own per-function doc
`src/processing/algorithms/portable_random.h:16-17`:

```
//   - boundedBelow / uniformIndex: single-draw modulo (documented bias
//     < 2^-32 relative for the draw ranges used here);
```

`uniformIndex` (lines 456–461) draws **two** engine values via `nextU64` — its own doc comment says so ("two draws via nextU64"). The summary bullet is wrong for `uniformIndex`. Fix the bullet to "boundedBelow: single-draw modulo; uniformIndex: two-draw u64 modulo".

### F3 — P3 — `rsShuffleAndKeep` comment claims consumers that do not exist (dead template)
`src/analysis/classification/rs_classification_utils.h:25-28` (updated in this diff):

```
/// Deterministic subsampling policy shared by training-data extraction
/// (per-class cap) and the K-Means operator (global cap): shuffle with the
/// caller's engine and keep the first \a maxKeep items.
```

Repo-wide grep (`src` + `tests`, on this branch **and** on master@911e0b17) finds **zero callers** of `rsShuffleAndKeep`: training extraction shuffles inline (`rs_training_data_extraction.cpp:122`) and the K-Means operator implements its own reservoir (`rs_kmeans_operator.cpp:269`). Two consequences: (a) the `Rng → std::mt19937` template tightening is compile/ABI-safe (nothing to break — verified, not assumed); (b) the rewritten comment still makes the false "shared by" claim. Fix the comment (or delete the dead helper in a follow-up).

### F4 — P3 — `boundedBelow` documents but does not enforce `bound > 0`
`src/processing/algorithms/portable_random.h:447-452`. `rng() % bound` with `bound == 0` is a division by zero (UB / SIGFPE) on a brand-new public header. No production caller today (only the test uses `bound=100`), but the header is the designated seam for future seed=42 paths. Add `if (bound <= 1) return 0;` (mirroring `uniformIndex`) or an assert.

### F5 — P3 — golden sequence pins only the vector Fisher-Yates, not the range overload
`tests/test_r22_science_random.cpp:77-99` pins exact permutations for `fisherYatesShuffle`; `fisherYatesShuffleRange` — the overload used by 4 of the 9 production call sites (all three in `rs_classification_split.cpp` plus future QVector-style callers) — is only checked to be *a* permutation (`tests/test_r22_science_random.cpp:142-146`). An off-by-one in the range version's `last - first` handling (e.g. shuffling n−1 elements) would still be a valid permutation and pass. Add one golden expectation for `fisherYatesShuffleRange` (it is bit-identical in draw usage by construction, but the pin should prove it).

### F6 — P3 — inaccurate "(raw draws only — portable)" claim in the numerics differential test
`tests/test_r22_science_numerics.cpp:53-56`: the comment says the test-local `std::mt19937` usage is "raw draws only — portable", but `wgen/hgen/fgen` are `std::uniform_int_distribution` — precisely the non-portable algorithm class #1685 documents. Harmless in effect (the oracle is self-consistent: whatever masks it generates are brute-force-compared within the same run), but the parenthetical overstates; drop it or say "distributions here are test-local; only self-consistency matters".

### F7 — P3 — ORACLES.md overstates the #1707 oracle as "no floating-point tolerance"
`.planning/r22-science/ORACLES.md` (Oracle 1: "平方距离完全一致（整数网格上精确相等，无浮点容差）"). The actual test compares `sqrt` **through the float round-trip with margin 1e-4** (`tests/test_r22_science_numerics.cpp:88`). The margin is justified (the #1707 defect produced errors ≥ 2 in squared units), but the doc should describe the real assertion.

### F8 — P3 — missing direct `<limits>` include in a new test
`tests/test_r22_science_numerics.cpp` uses `std::numeric_limits` (lines 38, 146, 147) but includes only `<cmath> <cstdint> <random> <vector>`; `<limits>` arrives transitively via `math_utils.h`. It compiles (verified by building the target), but the direct include is the hygiene the rest of the file follows.

### F9 — P3 — namespace alias declared mid-`#include`-block in the two operator TUs
`src/operators/rs/rs_kmeans_operator.cpp:26-30` and `src/operators/rs/rs_endmember_extraction_operator.cpp:11-16` place

```cpp
namespace r22portable = ::rs::algorithms::portable;
```

between `#include` directives. Legal C++, and the *reason* for the alias is sound and correctly diagnosed (inside `sicnu::operators::rs`, plain `rs::algorithms` would find the enclosing `sicnu::operators::rs` namespace — I confirmed the other modified TUs are at global/anonymous/`rs::algorithms` scope where unqualified use is safe). But the alias placement between includes is unconventional; move it below the include block (or use `::rs::algorithms::portable::` inline as `rs_register_images_operator.cpp` already does).

---

## Verified by independent recomputation

All of the following were recomputed or executed by the reviewer, not taken from the PR's own claims.

**#1707 (distance transform)**
- Re-derived the F-H lower-envelope data flow: with the new signature the envelope build reads only `f`, and the query loop writes only `out`; `f[v[j]]` can no longer read a transformed value. Caller wiring in `distanceToForeground` copies each row/col into `row`/`col` inputs and writes back from separate `rowOut`/`colOut` — no aliasing anywhere.
- Compiled the actual `distance_transform.cpp` standalone and ran: (a) the 3×4 issue counterexample passes exactly ({1,0,1},{2,1,0},{5,2,1},{8,5,4} squared — hand-derived independently first); (b) **20,000 randomized masks (up to 24×24, ~45% foreground) vs an O(N²) brute-force oracle: 0 mismatches**.
- Reconstructed the **pre-fix in-place pass** and confirmed it outputs 6 (not 8) at (3,0) — the RED provenance claim is real, not decorative.

**#1684 (accumulator stats / Kahan)**
- Checked the Kahan block in `rs_segment_features.cpp:290-301` term-by-term against the standard form (`y = v − c; t = s + y; c = (t − s) − y; s = t`): sign and order are correct for both `sum` and `sumSq`; compensators are zero-initialized together with the sums; consumption path (`computeStatsFromAccumulators` at line 521) is coherent.
- Executed the real `math_utils.cpp` on all five new test populations: all pass. Reproduced the old formula's failures exactly: **σ = 0.583961 (+16.8%)** for 10000.3±0.5 and **σ = 64.0005** for the 1e6±0.25 case — both matching the issue numbers cited in the diff (0.584, 64). Root cause confirmed: the old code squared the mean in **float** (`stats.mean * stats.mean`).
- The new-formula outputs: case 1 σ = 0.500848 (+0.17%, inside the documented <0.2% caller-accumulation floor); case 4 σ = 0.25 exact.

**#1685 (portable randomness)**
- Recompiled `portable_random.h` standalone and **re-derived every golden value in `test_r22_science_random.cpp` with a from-scratch pure-Python MT19937** (init_genrand seeding + tempering, no libstdc++ involved): first 8 u32 draws, 4 `nextU64` (hi=first-draw composition), 8 `boundedBelow(100)` values (42/67/76/14/26/35/20/24), all 4 `uniformUnit53` ((hi<<21)|(lo>>11))·2⁻⁵³ values, all 4 `normal01` Box-Muller cos values, and both Fisher-Yates permutations (seed 42 → {1,3,9,7,6,0,8,4,5,2}; seed 7 → {2,3,1,4,0}). **All exact matches.** I also hand-simulated the seed-42 shuffle step by step before running anything; it agreed.
- Fisher-Yates loop verified correct descending form: `i` from n..2, `j = rng() % i ∈ [0, i−1]`, swap `v[i−1]↔v[j]` — and it is bit-identical in draw usage to the existing hand-rolled convention in `rs_cross_validation.cpp` / `rs_spatial_cross_validation.cpp`, as the header claims.
- Call-site fidelity: kmeans reservoir `uniformIndex(rng, seen+1)` covers [0, seen] — same range as the old `dist(rng) % (seen + 1)`; RANSAC `uniformIndex(rng, size)` covers [0, size−1] — same as the old `pick(0, size−1)`; PPI kernel and operator draw **identical sequences** (mt19937(42), proj-outer/band-inner `normal01`, same norm-guard placement); split/training/multires sites are shuffle→shuffle replacements sharing one rng stream, as before. `uniformIndex` modulo is computed in u64 before the size_t cast, so 32/64-bit platforms agree.
- Namespace-safety of unqualified `rs::algorithms::portable` checked per TU: only the two operator TUs (inside `sicnu::operators::rs`) needed the alias — all other modified files are at global/anonymous/`rs::algorithms` scope.

**#1686 (resampler footprint)**
- Executed the real `resampler.cpp` on every new test expectation: bilinear at u=−0.25 → 1.5 (0.25 weight on clamped column 0 + 0.75 on column 0), u=3.25 → 30; **exactly ±0.5 is inside** (returns samples) and ±0.5+1e−9 is NoData for all four methods on both axes; nearest rounding at −0.4/3.4 → columns 0/3.
- Hand-derived and then executed the 4×4→8×8 warp: dst center (0,0) maps to (u,v)=(−0.25,−0.25) under the given geotransforms; corners are exactly f(0,0)=0, f(3,0)=30, f(0,3)=3, f(3,3)=33; all 64 cells valid; quarter-pixel translation keeps all 16 cells valid. Full 8×8 grid printed and inspected (values between corners are consistent bilinear mixes).
- Only remaining production `warpRaster` caller (`rs_register_images_operator.cpp:250`) writes the NoData sentinel for unmapped pixels and does not branch on the half-pixel ring — the footprint change is behavior-improving there, and `test_d14_geometric_registration_e2e` (verified set) covers it.

**Axis C — tests at risk that were NOT in the verified set**
The verified set omitted several binaries that exercise changed code. I built and ran all of them in the branch's own build tree (all pass):

| Binary | Assertions / cases | Why at risk |
|---|---|---|
| test_stratified_split | 125 / 4 | calls `stratifiedSplit` (3 shuffle sites changed) |
| test_training_data_extraction | 766 / 10 | per-class cap shuffle changed |
| test_raster_spatial5 | 87 / 13 | `rs:proximity` → `distanceToForeground` (#1707) |
| test_segment_features | 104 / 10 | Kahan accumulators (#1684) |
| test_classification_intelligence_e2e | 6202 / 5 | uses `stratifiedSplit` |
| test_simple_segmenter / test_pixel_ignore_options | 85 / 22 | segmenter discipline paths |
| test_obia_integration / test_obia_operators | 297+170 / 21 | OBIA pipeline (segmenter + features) |
| test_recipe_equivalence / test_obia_architecture | 79+13 / 8 | end-to-end recipe runs |

I also re-ran the author's entire verified set myself, including the three new binaries (`test_r22_science_numerics` 351,467 assertions / 7 cases; `test_r22_science_random` 201,069 / 6; `test_r22_science_resampler` 53 / 5) and `test_primitives5`, `test_math_utils`, `test_resampler`, `test_feature_matcher`, `test_endmember_extraction`, `test_classifier_kmeans`, `test_classification_pipeline`, `test_obia_segmentation`, `test_d14_geometric_registration_e2e`, `test_rs_operators` (82/82 with PROJ_DATA provisioned; 4 initially-observed failures were my sandbox's missing proj.db, reproduced and eliminated with `PROJ_DATA` set — environmental, unrelated to the diff). Assertion audits: I read the at-risk tests' assertions; the RNG-adjacent ones are structural (counts, same-seed determinism, leakage) and cannot fail on a permutation change, and the proximity test pins exact-correct distances that the fixed EDT also produces (subsumed by my 20k-mask differential).

**Axis D — API/ABI/header safety**
`rsShuffleAndKeep` signature change: zero callers repo-wide (see F3). `AccumulatorStats` change is doc-only (struct layout untouched). `portable_random.h` includes only `<cmath> <cstdint> <random> <utility> <vector>` — no Qt, no project headers, no cycles. Test CMake registration verified by building and executing all three targets (`qt_add_executable` + `Catch2::Catch2WithMain` + `Qt6::Core` + `sicnu_processing`, appended at end of `tests/CMakeLists.txt` after the R20-05 block — append-only as required; `sicnu_discover_tests` exists and discovery succeeded).

**Axis E — file whitelist**
`git diff --name-only` over the review range yields exactly: `.gitignore` (whitelist lines only), `.planning/r22-science/*.md`, `src/analysis/classification/{rs_classification_split.cpp, rs_classification_utils.h, rs_training_data_extraction.cpp}`, `src/analysis/segmentation/{rs_multires_segmenter.cpp, rs_segment_features.cpp}`, `src/operators/rs/{rs_endmember_extraction_operator.cpp, rs_kmeans_operator.cpp}`, `src/processing/algorithms/**` (7 files), `tests/{CMakeLists.txt, test_r22_science_*.cpp ×3, test_resampler.cpp}`. Nothing outside the whitelist. The `.gitignore` stanza mirrors the existing r20-05 pattern.

**Axis F — test quality**
Golden values: exact (see #1685 above). Tolerances: case-1 ε=1% measured floor +0.17%; case-4 ε=1e-4 with exact reconstructed sums; #1707 margin 1e-4 vs defect magnitude ≥ 2 squared units; #1686 1e-6/1e-12 margins are exact-arithmetic checks. None of the tolerances could mask a reintroduction of the fixed defects — each was checked against the reproduced pre-fix failure magnitude. The `while (cases < 3200 || withForeground < 3000)` loop terminates a.s. (P(no fg) ≤ 0.55⁴⁰⁰ per case). Runtime of the differential case is a few seconds (I executed it at 6× the case count).

---

## Summary table

| # | Severity | File:line | One-liner |
|---|---|---|---|
| F1 | P2 | src/processing/algorithms/resampler.h:39 | header contract still documents the old pixel-center footprint #1686 removed |
| F2 | P3 | src/processing/algorithms/portable_random.h:16 | summary calls uniformIndex "single-draw"; it is two-draw |
| F3 | P3 | src/analysis/classification/rs_classification_utils.h:26 | rsShuffleAndKeep has zero callers; "shared by" comment is false |
| F4 | P3 | src/processing/algorithms/portable_random.h:449 | boundedBelow: `bound==0` is UB, documented not enforced |
| F5 | P3 | tests/test_r22_science_random.cpp:142 | range-overload Fisher-Yates not sequence-pinned |
| F6 | P3 | tests/test_r22_science_numerics.cpp:53 | "raw draws only — portable" claim inaccurate |
| F7 | P3 | .planning/r22-science/ORACLES.md | "no floating-point tolerance" overstates the 1e-4-margin oracle |
| F8 | P3 | tests/test_r22_science_numerics.cpp:38 | std::numeric_limits used without direct `<limits>` |
| F9 | P3 | src/operators/rs/rs_kmeans_operator.cpp:29 | namespace alias declared mid-include-block |

Recommendation: fix F1 before merge (one-line doc edit); F2–F9 can be follow-ups. No correctness, portability, thread-safety, or registration defects found in the four fixes themselves.
