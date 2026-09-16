# REVIEW_LOG — spectral-intelligence-11

## Round 1 — independent adversarial review (read-only subagent, full `origin/master...HEAD` diff)

| # | Sev | Finding (file:line) | Disposition | Fix |
|---|---|---|---|---|
| 1 | P0 | Full-mode RX scaled loading used raw scatter trace (α(N−1)tr(Σ)/B), not the documented α·tr(Σ)/B; test reference mirrored the bug (spectral_local_rx.cpp:153-166, test_spectral_local_rx.cpp:131-141) | **FIXED** | Normalize covariance first, then accumulate trace — in kernel AND test reference; guard/injection semantics re-derived and verified |
| 2 | P0 | 4 new task-declaring operators but no regenerated `algorithm_meta` sidecars → `test_algorithm_meta_drift` red (data/processing/algorithm_meta/) | **FIXED (in progress at review)** | `sicnu_geo_rs_cli --export-catalog` regeneration + pin reconciliation (blocked on Windows CLI build; completed this round) |
| 3 | P1 | "Batch driver matches per-pixel path" test ignored default λ=0.01 → asserted x instead of max(x−λ,0) (test_spectral_sparse_unmixing.cpp:200-209) | **FIXED** | Set `config.lambda = 0.0` with comment (identity dictionary: a == x) |
| 4 | P1 | Orthogonal disjoint-support spectra: SID=0 by master convention, tan(π/2) finite ≈ 1.6e16 → hybrid 0.0, `CHECK(isinf)` failed; ClassicTan ranked orthogonal == identical (spectral_hybrid_similarity.cpp) | **FIXED (kernel)** | ClassicTan special-cases θ ≥ π/2−ε → +inf (zero cosine similarity = maximal dissimilarity, documented in-code); test now passes with correct semantics |
| 5 | P1 | Pipeline test expected 1-based labels; operator (matching master sam_classify) writes 0-based (test_spectral_pipeline_11.cpp:270-281) | **FIXED (test)** | Assertions updated to 0-based (class 0 vegetation / class 1 soil, floor −0.5); contract documented as master-convention |
| 6 | P2 | `outputDigest` echoed empty (SpectralTable::save computes digest in toJson, never writes back) (rs_endmember_analysis_operator.cpp:317) | **FIXED** | `out.digestHex = SpectralTable::digestHex(...)` before validate/save |
| 7 | P2 | Sparse operator left partial rasters on cancel/failure; empty `try/catch{throw;}` shell (rs_sparse_unmixing_operator.cpp:200-266) | **FIXED** | Catch closes datasets + `QFile::remove` of both output paths, then rethrows |
| 8 | P2 | Power-iteration Lipschitz could undershoot (orthogonal start vector) → wrong stable solution (spectral_sparse_unmixing.cpp:181-206) | **FIXED** | Replaced with analytic `||G||∞` bound (λmax ≤ ∞-norm for symmetric PSD, always safe, deterministic); degenerate Gram refuses |
| 9 | P2 | Signed int overflow in `outerWindow²·bands` reserve (spectral_local_rx.cpp:289) | **FIXED** | size_t casts on every factor |
| 10 | P3 | Kernel→driver degeneracy signaling via error-string matching (spectral_local_rx.cpp:338-341) | **FIXED** | `PixelScoreStatus` enum out-param; driver switches on status |
| 11 | P3 | Wavelength guard inert on operator surface; CHANGELOG oversold | **FIXED (docs)** | CHANGELOG wording scoped to kernel-level guard; operator relies on reference-seam resampling |
| 12 | P3 | Present-but-wrong-typed optional params silently ignored (rs_endmember_analysis_operator.cpp:110-132) | **FIXED** | `angleMatrix`/`requireFullCoverage`/`ppiCounts` type-checked → InvalidParameter |
| 13 | P3 | `streamingHaloPixels()` returned fixed 16 regardless of window size (rs_local_rx_operator.h:51-55) | **FIXED** | Override removed (operator self-manages halo via padded reads; framework halo honest 0) |
| 14 | P3 | (a) meanScore silently 0 without scoreOut (rs_spectral_similarity_operator.cpp:186); (b) quality plane 0.0 for invalid centers indistinguishable from empty windows (rs_local_rx_operator.cpp:270) | **FIXED** | (a) scores always computed; (b) new `centerValid` plane in kernel → NaN quality for invalid centers |
| — | — | Extra (found during fix round): test-local Gauss–Jordan `swap_ranges` on identical ranges = UB crash under MSVC Debug iterators ("ranges should not overlap", xutility:4503) — crashed cases calling `referenceScore` | **FIXED** | Guarded manual row swap when pivot != col |
| — | — | Extra: scale-test diagonal case unscored (background 16 < auto min 1025) at test_spectral_scale.cpp:237 | **FIXED** | Explicit `minBackgroundSamples = 8` (documented knob) with rationale comment |

## Verified-clean (reviewer-confirmed)

FISTA prox/momentum; sum-to-one augmentation matches master FCLS convention; all closed forms recomputed by hand (soft-threshold, orthogonal per-atom, arccos(4/5), (2/3)ln2, linear 1.5/2.5, 5×5−3×3=16); average-link is genuine UPGMA; angle-matrix symmetry; 8192-band guard boundary; capability JSON schema compliance (`uncertainty`/`table` in vocab); CMake source lists complete; AUTOMOC coverage; padded-halo tiling exactly tile-agnostic; registration dual sites consistent; pipeline_run_coordinator include fix minimal and correct.

## Round 2 — post-fix re-verification

- All targeted suites rerun twice after fixes (Phase 8): see TEST_MATRIX exit columns.

Disposition rule: P0 = must fix before PR (all fixed); P1 = must fix (all fixed); P2 = fixed; P3 = fixed or documented.
