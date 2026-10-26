# R22-01 science track — Review Round 2 (independent adversarial review)

- Reviewer: R2, independent of both the author and R1; R1's report
  (`.planning/r22-science/REVIEW_R1.md`) was read but **nothing was taken from it
  without independent verification** where verification was possible on this host.
- Worktree: `agent/r22-science-20261025T133250Z`, review range
  `911e0b17a3533ca0d79c1b68b000d6052c4c3c61..HEAD` (5 commits incl. R1-closure
  `eb4187028d`).
- Method: failure-counterexample construction against the real TUs (standalone
  harnesses compiled from the actual `src/` files, scratch in `/tmp/r22` only),
  audit of the R1-closure commit for newly introduced defects, and a re-run of the
  at-risk test surface (33 test executions — superset of both the author's and
  R1's sets).

## Verdict: **APPROVE**

No P0/P1/P2 defects found. Two P3 documentation-precision findings (G1, G2), one
of which (G1) was **missed by R1 and retained verbatim through the R1-closure
commit**. All four fixes survive genuinely adversarial input: 111,022 randomized
EDT masks compared bitwise-exact against brute force, 21.4M+23.6M output cells,
zero mismatches; every RNG golden re-derived from a from-scratch
standard-description MT19937; the #1686 footprint verified as the exact
complement of the documented region at one-ulp precision over 1.28M samples per
method; the #1684 caller-floor claims measured honest at condition numbers up to
1e30. The R1-closure commit introduces no functional defect (details in §5).

---

## Counterexample log

### 1. #1707 — exact EDT vs brute force (real `distance_transform.cpp`)

Harness: `/tmp/r22/edt_fuzz.cpp` + `/tmp/r22/edt_exact.cpp`, compiled with the
real `src/processing/algorithms/primitives/distance_transform.cpp` (g++ 16.2.1,
-O2, C++17).

| Run | Cases | Output cells | Comparison | Mismatches |
|---|---|---|---|---|
| tol-run (float-round-trip tol) | 60,020 | 23,631,673 | `\|d²−ref\| ≤ ref·2.2e-7` | **0** |
| bitwise-run | 51,002 | 21,393,289 | `out[i] == (float)std::sqrt(ref)` **bitwise** | **0** |

Coverage: sizes 1×1 … 40×40 uniformly random; densities swept 1%…99% (uniform
per-case plus forced 1%/99% extremes, 4,000 cases); degenerate 1×1 (fg and bg),
1×100 / 100×1 / 40×1 (fg at end, middle, none, all-fg); single foreground at 12
corner/edge positions of a 9×7; all-foreground 40×40; odd/even 39×39
checkerboards; 2,000 long thin strips (n=2…64) in both orientations. Foreground
cells exercised: 11.8M. No-foreground masks correctly yield +inf everywhere.

**Pre-fix RED provenance re-verified**: the master (`911e0b17`) in-place
`squaredDistancePass` reconstructed from the real pre-PR file outputs squared
distance **6** at (3,0) for the issue's 3×4 mask (truth 8) — the reported defect
is real, not decorative (`/tmp/r22/prefix_probe.cpp`).

### 2. #1684 — accumulator stats (real `math_utils.cpp`)

Harness: `/tmp/r22/stats_fuzz.cpp`, `/tmp/r22/old_real.cpp` (runs the **actual
pre-PR TU** from `git show 911e0b17:...math_utils.cpp`).

Degenerate inputs — all pass, no crashes:
- N=0 → mean/stddev 0; N=1 → exact mean, stddev 0.
- count=4, sumSq<sum²/N (variance −0.25) → clamped to stddev 0 (finite).
- NaN sum → mean NaN, stddev 0 (documented non-finite clamp); Inf sumSq →
  stddev 0; Inf/Inf → mean inf, no crash. (Pre-existing clamps, unchanged by PR.)

Caller-floor honesty (the header's central claim), alternating ±d populations,
σ relative error:

| population | cond (mean²/σ²) | naive sums | Kahan sums | exact-reconstructed |
|---|---|---|---|---|
| c=1e4, σ=0.5, N=1e6 | 4e8 | 1.7e-3 | 0 | 0 |
| c=1e6, σ=0.25, N=2e5 | 1.6e13 | **9.2e-1** | **0** | **0** |
| c=1e9, σ=0.5, N=1e6 | 4e18 | 1.0 | 1.0 | 1.0 |
| c=1e12, σ=1, N=1e6 | 1e24 | 3.7e6 | 1.0 | 1.0 |

Reading: at cond ≤ 1.6e13 the degradation is **exactly caller-side accumulation**
(Kahan/exact sums recover σ exactly at the same condition number where naive
fails at 92%) — the header's honesty claim holds precisely. At cond ≥ ~1/ε_dbl
(4.5e15) no caller precision can save the two-sum form (exact sums still fail) —
see finding G2 about where the docs attribute this. The header's threshold
"naive dominates for mean/σ > ~1e6 at N=1e6" matches measurement (mean/σ=2e4 →
0.17% error; 4e6 → 92%). A random (non-alternating) N(5e9,1) population, N=1e6,
confirms the same picture (naive 522× off; shifted-accumulation form limited by
the double floor).

Old-formula reproduction through the **real pre-PR TU**: σ=0.583961 (+16.8%) for
the test's 10000.3±0.5 population — matches R1's number exactly — and σ=64.072
(N=1e6) / 64.000 (N=2e5) for the 1e6±0.25 class (issue said 64). Note the old
bug's magnitude depends on `stats.mean * stats.mean` being a **float** multiply;
a re-typed double-multiply variant of the old formula gives 2.04 instead of
0.584 — the class of failure is robust either way.

### 3. #1685 — portable randomness

(a) **From-scratch MT19937** (`/tmp/r22/mt19937_indep.py`, written from the
standard description only — init_genrand 1812433253 recurrence, twist with
0x9908b0df/upper-31/lower-31, tempering 11/7-0x9d2c5680/15-0xefc60000/18; no
library, no R1 artifacts). **Every golden in `test_r22_science_random.cpp`
matched exactly**: 8 raw u32 (seed 42), 4 `nextU64` (hi=first-draw composition),
8 `boundedBelow(100)` (42/67/76/14/26/35/20/24), 4 `uniformUnit53`
(bit-identical repr), 4 `normal01`, Fisher-Yates seed 42 n=10
{1,3,9,7,6,0,8,4,5,2}, seed 7 n=5 {2,3,1,4,0}, and the **new range-overload
golden added by eb4187028d** seed 42 n=8 **{1,0,3,7,5,4,2,6}** — independently
confirmed correct. Moments seed 123: mean −8.3e-4, σ 0.9987 (both within the
test's bounds).

(b) **libc++ check: NOT POSSIBLE on this host.** clang 23.1.1 is present, but
`clang++ -stdlib=libc++` cannot find `<vector>`; no `libc++` headers under
`/usr/include/c++/v1`, no `libc++.so`/`.a` anywhere under `/usr/lib` or
`/usr/local`, and no libc++ package installed (pacman). Stated as required. The
strongest available substitute was executed instead: (i) the golden sequences
were re-derived by a **non-C++** from-standard MT19937 (Python) — proving the
pins encode the standard's engine, not a libstdc++ quirk; (ii) the real header
+ goldens compiled and run under **clang 23 vs gcc 16** (both libstdc++) with
identical results (`/tmp/r22/random_edges.cpp`).

(c) Real-header edge probes (gcc+clang): `boundedBelow(0)/(1)` and
`uniformIndex(0)/(1)` return 0 **without consuming a draw** (next raw draw is
still 1608637542 — the new eb4187028d guard's no-draw contract is real);
`uniformIndex` at bound = 2³², 2³²−1, 2⁶³, 2⁶⁴−1 all in-range; Fisher-Yates
size 2 balanced (1012/2000 swaps), size 1/0 no-ops; `normal01` finite over 1e6
draws (range [−4.80, 4.60]); `nextU64` order hi-then-lo verified against raw
draws (initial probe had a precedence bug in my own check — redone cleanly).

### 4. #1686 — resampler footprint (real `resampler.cpp`)

Harness: `/tmp/r22/resample_fuzz.cpp` (g++ -O2 C++20, links the real TU). The
diff over master is **only the two boundary guards**, so old behavior is exactly
reconstructible (`old(u,v) = noData` outside `[0,w−1]×[0,h−1]`, else the new
code path).

- **Exact complement**: dense (u,v) sweep over [−1.2, w+0.2]×[−1.2, h+0.2],
  step 0.013, 5×7 all-distinct raster, all four methods: 1,275,884 samples —
  830,060 inside / 445,824 outside, **NoData ⇔ outside `[-0.5, 4.5]×[-0.5, 6.5]`
  with zero exceptions**. Boundary inclusive at exactly ±0.5 for all methods;
  one-representable-double outside → NoData, one inside → valid (both axes, all
  methods). NaN/Inf coordinates → NoData. 486,160 sentinel-free in-footprint
  samples never produced the sentinel; bilinear output provably within the
  clamped-tap min/max envelope everywhere.
- **Only-adds claim verified**: with sentinel-bearing rasters (left column =
  NoData; all border = NoData), the old-valid region `[0,w−1]×[0,h−1]` shows
  **0 value changes** across the full sweep for every method; the new half-pixel
  ring (where old was 100% NoData by construction) now returns VALUE on
  294,372 / 462,672 ring samples (left-col sentinel) and NoData on 168,300;
  with an all-sentinel border the ring stays NoData (nothing valid to sample).
  New-valid ⊇ old-valid everywhere — the change never removes a value, and
  clamped sampling of sentinel edges forces NoData **less** often than before,
  never more.
- **Renormalization interaction characterized**: at u ∈ (−0.5, 0) bilinear's two
  x-taps (columns {−1, 0}) **both** clamp onto column 0; if column 0 is the
  sentinel, `validWeight = 0` and the half-valid gate fails → NoData. There is
  **no** bogus synthesis (no `0/validWeight` leak) anywhere on the ring, and the
  interior renormalization rule (u ∈ (0, 0.5), taps {0,1}) is bit-identical to
  the old behavior. Two of my initial probe assertions were wrong (a one-ulp
  direction bug and a mis-predicted "synthesizes 0.0" outcome) — both were my
  harness's errors, resolved analytically and documented here for honesty.
- Warp-corner re-derivation (independent of R1): dst (0,0) center → world
  (0.25, −0.25) → src (u,v) = (−0.25, −0.25); both tap axes collapse onto
  col/row 0 with weights 0.25/0.75 → exact f(0,0)=0, matching the pinned corner
  expectations; ditto the other three corners (30/3/33).

### 5. R1-closure commit `eb4187028d` — new-defect audit

Read the full commit diff (src+tests, 13 files). Checks performed:

1. **boundedBelow guard** (`bound <= 1 → 0`, no draw): the bound=100 golden path
   is untouched (my re-derivation matches 42/67/76/14/26/35/20/24); no
   production caller exists whose stream alignment could depend on the old
   draw-consuming behavior (repo-wide grep: `boundedBelow` has **zero**
   production callers; `uniformIndex` callers always pass bound ≥ 2 — kmeans
   `seen+1 ≥ k+1`, RANSAC `size() ≥ 4`). No new defect. The no-draw contract is
   additionally pinned by the new test (`probe() == 1608637542` — comment
   accurate).
2. **Alias move (F9)**: pure code motion from mid-include-block to below the
   include block in both operator TUs; the alias is TU-local, no header included
   after the old position references it, and first use is inside function bodies
   after the new position. Compile-verified by rebuilding all affected targets
   (33 binaries). No semantic change.
3. **New range-FY golden {1,0,3,7,5,4,2,6}**: verified correct with my
   independent MT19937 (§3a). The vector and range overloads are draw-identical
   by construction, as both goldens independently confirm.
4. **F1 header text vs implementation**: `resampler.h` now documents
   `[-0.5, width-0.5] × [-0.5, height-0.5]` with edge-clamped taps — matches the
   guards exactly (inclusive boundary verified at one-ulp precision, §4), and
   "taps are edge-clamped" is accurate for all four methods. Correct closure.
5. F2/F6/F7/F8 doc/include edits: reviewed; no inaccuracy introduced by the
   edits themselves — but the F2 rewrite **retains a wrong number** on
   `boundedBelow` (finding G1 below).
6. The closure commit's claim "random 201,081 assertions" matches my run
   (201,081; R1-era count was 201,069 — the +12 are the new pins). numerics
   351,467 and resampler 53 also match.

### 6. Repo hygiene

- `tests/CMakeLists.txt`: append-only block at EOF after the R20-05 stanza
  (verified in diff); `sicnu_discover_tests` on all three new targets.
- File whitelist over the range: exactly the 21 src/tests/.gitignore files
  listed by `git diff --name-only` (plus `.planning/r22-science/*.md`); nothing
  outside. `.gitignore` stanza mirrors the r20-05 pattern (markdown-only
  allowlist).
- **No test pins OLD (pre-PR) sequences or outputs**: `test_primitives5` /
  `test_raster_spatial5` pin mathematically-correct closed-form distances
  (1, √2, 2, √5, √8 …, margin 1e-6) — correct-answer pins, unchanged files,
  satisfied exactly by the fixed kernel (subsumed by my bitwise differential).
  kmeans/endmember/feature-matcher/split/training/multires tests assert
  structure (sizes, counts, stratification ratios, run-to-run determinism) or
  tolerance-based recovery of known ground truth — nothing RNG-sequence- or
  edge-row-exact. The one modified existing test (`test_resampler.cpp` line
  106) moved its out-of-range pin from −0.25 to −0.75 with an explanatory
  comment: the old pin encoded the very defect #1686 fixes; the new in-ring
  behavior is positively pinned by `test_r22_science_resampler.cpp`. This is a
  legitimate semantic update, not a weakening.
- **Binaries executed** (all PASS; author's wrapper env `/tmp/r22s-run.sh`,
  `QT_QPA_PLATFORM=offscreen`, PROJ provisioned):

| binary | assertions / cases | note |
|---|---|---|
| test_r22_science_numerics | 351,467 / 7 | |
| test_r22_science_random | 201,081 / 6 | +12 vs R1 (closure pins) |
| test_r22_science_resampler | 53 / 5 | |
| test_resampler | 434 / 13 | |
| test_primitives5 | 171 / 17 | |
| test_math_utils | 77 / 6 | |
| test_feature_matcher | 58 / 4 | |
| test_endmember_extraction | 72 / 4 | |
| test_classifier_kmeans | 2,202 / 4 | |
| test_classification_pipeline | 240 / 26 | |
| test_stratified_split | 125 / 4 | |
| test_training_data_extraction | 766 / 10 | |
| test_raster_spatial5 | 87 / 13 | |
| test_segment_features | 104 / 10 | |
| test_classification_intelligence_e2e | 6,202 / 5 | |
| test_simple_segmenter / test_pixel_ignore_options | 85 / 13, 22 / 4 | |
| test_obia_integration / test_obia_operators | 297 / 3, 170 / 18 | |
| test_recipe_equivalence / test_obia_architecture | 79 / 5, 13 / 3 | |
| test_obia_segmentation | 850 / 14 | |
| test_d14_geometric_registration_e2e | 8,351 / 3 | |
| test_rs_operators | 9,081 / 82 | PROJ_DATA provisioned |
| **test_execution_benchmarks** ("ebench proximity") | 16 / 1 | **run by neither author nor R1** |
| **test_contract_determinism_11** | 138 / 4 | **not in either prior set** |
| **test_science_verification_10** | 1,187 / 5 | **not in either prior set** |
| **test_scientific_contract_10** | 1,762 / 5 | **not in either prior set** |
| **test_verifier_packs_14** | 299 / 10 | **not in either prior set** |
| **test_mnf_transform** | 1,879 / 7 | **not in either prior set** |
| **test_spatial_cross_validation** | 592 / 7 | unchanged-kernel control |
| **test_challenger_m1_2_harness** | 9/9 suites | **not in either prior set** |
| **test_curriculum** | 349 / 10 | **not in either prior set** |

No binary that exercises changed code was left unrun; none failed. Remaining
unexecuted tests (GUI-shell families etc.) contain no changed-file consumers per
closure analysis and assert no exact values from the changed kernels.

---

## Findings

### G1 — P3 — `boundedBelow`'s documented modulo-bias bound is mathematically wrong (and the wrong half of the old F2 sentence was kept)
`src/processing/algorithms/portable_random.h:18-19` (summary) and `:48-50`
(function doc), introduced in `e79455d51e`, **retained verbatim by eb4187028d's
F2 closure edit**.

For a single 32-bit draw modulo `b`, the worst-case relative bias is
`(b − (2³² mod b)) / 2³²` (0 when b divides 2³²). Concretely: b=100 — the only
bound used anywhere today (test-only; zero production callers) — gives
4/2³² = **2⁻³⁰, exactly 4× the documented "< 2⁻³²"**; b=10⁶ gives 7.6e-6
(~2¹⁷× the claim); as b → 2³²⁻ the relative bias → +100%. The 2⁻³² figure is in
fact the correct bound for **`uniformIndex`** (mod 2⁶⁴, bound ≤ 2³² → relative
bias < 2⁻³²) — i.e. pre-F2 the shared bullet was half-true, and the F2 rewrite
moved the true half off `uniformIndex`'s bullet while leaving the false number
on `boundedBelow`. Related nit in the same header (`:59`): uniformIndex's
"same negligible class as boundedBelow" — it is ~2³²× smaller (mod 2⁶⁴ vs
mod 2³²). Zero runtime impact today; a one-line doc fix
("bias < bound/2³² relative — 2⁻³⁰ at bound 100; use uniformIndex for size_t
bounds") closes it. This is the only defect class R1's F2 touched but did not
actually fix.

### G2 — P3 — `AccumulatorStats` header attributes the whole residual error to the caller; above cond ≈ 1/ε_double the formula itself is the floor
`src/processing/algorithms/math_utils.h:30-32` ("The residual error therefore
comes from the CALLER's accumulation of sum/sumSq").

Measured (§2 table): at cond = mean²/σ² ≤ 1.6e13 the claim is exactly right
(Kahan/exact sums recover σ to 0% where naive errs 92%). At cond ≥ 4e18
(c=1e9, σ=0.5) **error-free reconstructed sums still produce 100% σ error**, and
Kahan fails identically at c=1e12 — the `sumSq/N` and `sum/N` roundings plus the
double-precision cancellation floor dominate, which is *not* caller-side. The
sibling comment in `math_utils.cpp:161-163` states this correctly
("~2·ε·(mean²/σ²) relative … a property of the AccumulatorStats contract, not of
this function"), so the two new doc blocks disagree about where the floor lives.
Doc-only; suggested fix: have the header point at the cpp's condition-number
floor instead of attributing all residual error to the caller.

---

## R1 claims: verified independently vs taken on faith

**Independently verified (re-executed or re-derived by me):**
- #1707 algorithmic correctness — 111,022 masks / 45M cells vs R1's 20k, with a
  **bitwise** (not tolerance) comparison; pre-fix RED value 6≠8 reproduced from
  the real pre-PR TU.
- #1684 degenerate cases, caller-floor honesty at cond 4e8…1e30, Kahan-vs-naive
  separation, and R1's exact old-formula number 0.583961 (via the real pre-PR
  TU; the "≈64" class reproduced, exact digits vary with population details).
- Every RNG golden in the test file (including the post-R1 range-FY pin) via a
  from-scratch Python MT19937; boundedBelow guard no-draw semantics; FY size-2;
  uniformIndex boundary bounds; nextU64 composition order; 1e6-draw normal01
  finiteness; cross-compiler (gcc/clang) agreement.
- #1686 footprint complement at one-ulp precision (1.28M samples/method), the
  only-adds claim with zero interior changes, the ring-renormalization
  behavior, and the 8×8 warp corner values (re-derived).
- eb4187028d specifics: bound=100 goldens unchanged, alias move inert, new
  golden correct, F1 header text matches implementation exactly.
- Zero production callers of `rsShuffleAndKeep` and `boundedBelow` (F3/F4
  context); PPI kernel/operator draw-sequence identity (read both code paths:
  same `bands` normal01 draws, same `norm < 1e-12 → skip` guard).
- CMake append-only registration; file whitelist; `.gitignore` pattern; no
  stale pre-PR pins in any test; TEST_RESULTS.md assertion counts match my runs.
- Every at-risk binary R1 listed, plus 9 further binaries neither prior party
  ran (all green).

**Taken on faith (could not or did not re-execute on this host):**
- libc++-buildability of the pinned sequences (libc++ absent from this host;
  stated in §3b, substituted with the from-standard re-derivation + clang/gcc
  cross-check).
- R1's runs of GUI-shell test families neither of us executed (no changed-code
  consumers per closure analysis; no exact-value pins found by grep).
- MSVC/macOS behavior claims (single-host review, as for R1 and the author).

## Summary table

| # | Severity | File:line | One-liner |
|---|---|---|---|
| G1 | P3 | src/processing/algorithms/portable_random.h:18 | boundedBelow bias "< 2^-32 relative" is wrong (2^-30 at the only used bound; → +100% near 2^32); the true 2^-32 bound belongs to uniformIndex |
| G2 | P3 | src/processing/algorithms/math_utils.h:31 | header attributes all residual error to the caller; above cond ~1/ε the formula's own floor dominates (cpp comment states it correctly) |

Recommendation: **APPROVE**. G1/G2 are one-line doc fixes that can be follow-ups;
neither affects any executed code path today. The R1-closure commit introduced no
functional defect.
