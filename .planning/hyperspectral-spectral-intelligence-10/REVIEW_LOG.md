# REVIEW_LOG — hyperspectral-spectral-intelligence-10

Findings → disposition → evidence. Phase 7 ran two read-only subagents:
A = architecture + scientific correctness; B = performance/concurrency/
lifecycle/test-trustworthiness. Both reports received 2026-09-13; every
finding was verified in code by the main agent before disposition.

| ID | Sev | Area | Finding | Disposition | Evidence |
|---|---|---|---|---|---|
| A-P0-1 | P0 | seam | FWHM-absence encoded as (0,"") placeholders -> gridFromBandValues refuses; every WAVELENGTH-tagged raster without FWHM hard-refused all four consumer operators (incl. previously-working inline calls) | fixed | rs_spectral_reference_input.cpp: FWHM vector only built when ALL bands carry it; partial coverage falls back to linear (documented optional metadata) |
| A-P0-2 | P0 | MNF | RowFeeder counted samples in BOTH passes -> covariance divisor 2N-1 -> reported SNR halved | fixed | finalizeMean() resets the sample counter; header doc updated; scale fixture enlarged to pixels>bands (the fix also let the rank-sufficiency refusal work as designed) |
| A-P1-1 | P1 | MNF inverse | dropped-mass errorOut/reconstructionError identically zero for k<B inputs (unknown coefficients silently "perfect") | fixed | droppedMassUnknown flag: errorOut=NaN + payload flag + note naming the widths |
| A-P2-1 | P2 | MNF inverse | component indices beyond input width silently read zero-padding and escaped the dropped-mass mask | fixed | parseComponents bounds indices by the KNOWN width; typed refusal naming index + input width; regression-tested in test_spectral_pipeline |
| A-P2-2 | P2 | table | validate() digest walk could over-read rows before width failures were respected | fixed | digest check skipped when any row width is inconsistent (widths already reported) |
| A-P2-3 | P2 | unmixing | documented abundance-sum QA missing from payload | fixed | operator accumulates mean \|sum(a)-1\| over valid pixels; `abundanceSumDeviation` in payload |
| A-P2-4 | P2 | wavelength | gridFromBandValues did not enforce documented positivity of centers | fixed | first center <= 0 -> NonFinite refusal |
| A-P3 | P3 | misc | per-band NoData in mnf_inverse (band-1 only); detection provenance echo missing; measured-echo overstated for mixed libraries; uniform-license echo counted empty licenses; library_select "clip" wording; ARCHITECTURE near-dup provenance wording; FCLS refusal tolerance unnamed; sizeInt overflow; MNF digest did not cover snr/noise/wavelengths | fixed (all) | per-file edits; digest extended + documented |
| B-P1-A1 | P1 | lifecycle | 4 operators closed outputs with close() — ENOSPC flush failures logged and reported as success (truncated GeoTIFF published) | fixed | closeWithError() on all success paths of rs:mnf, rs:mnf_inverse, rs:spectral_band_select, rs:spectral_unmixing |
| B-P1-A2 | P1 | lifecycle | no partial-output cleanup on throw/cancel — phantom half-written rasters at deliverable paths | fixed | new rs_partial_output_guard.h (RAII remove-until-disarm) wired into rs:mnf (output+transform artifact pair), rs:mnf_inverse, rs:spectral_band_select |
| B-P2-A3 | P2 | seam | kMaxCells enforced only on table loads; library loads + resampled outputs unbounded; bad_alloc escapes typed-error contract | fixed | cell bound enforced on library materialization and on the resampled output shape (typed refusals); 256 MiB pre-read bound on reference/table files |
| B-P2-A4 | P2 | PPI | projections/nEndmembers/tile buffer unbounded (multi-GB allocations escape typed errors) | fixed | caps 100000 projections / 1024 endmembers / 4096 bands, typed refusals; executionEstimate notes the band-linear tile term |
| B-P2-A5 | P2 | FCLS | NNLS heap churn per pixel/iteration | fixed (moderate) | scratch vectors + members hoisted to per-call reuse |
| B-P2-B1 | P2 | MNF | artifact/raster pair consistency on pass-3 failure | fixed | pass-3 failures now remove BOTH the partial raster and the transform artifact via the guard |
| B-P2-C1 | P2 | tests | FCLS suite could not distinguish the true constrained solver from OLS+clip+renormalize | fixed | new known-answer: x=[2,1,0] on e1=[1,0,0],e2=[0,1,0] -> analytic constrained optimum [1,0] vs [2/3,1/3] for the degenerate method (hand derivation in test) |
| B-P2-C2 | P2 | tests | rs:mnf_inverse + rs:mnf transformOut had zero operator-level coverage | fixed | test_spectral_pipeline MNF chain E2E: forward artifact -> raster roundtrip (epsilon 1e-4), subset selection with errorOut>0, spectrum mode, P2-1 refusal |
| B-P3-C3 | P3 | tests | scale test overclaimed a memory assertion; two near-vacuous RMSE checks | fixed | comment corrected; assertions strengthened to monotone dropped-mass with positivity |
| B-P3-C4 | P3 | tests | MNF digest did not cover snr/noiseEigenvalues/wavelengths (tamperable) | fixed | digest extended over all persisted numeric fields |
| B-P3-A6/A7/A8, B2, B3, C5 | P3 | misc | estimate understatement; redundant digest recomputation; unchecked artifact write; Jacobi cancellation gap; artifact namespace pollution; >256 near-dup path untested | accepted with reasons | estimates corrected (A6); artifact write checked (A7b); digest recomputation accepted (bounded, k<=O(100) spectra typical); Jacobi hook accepted (pure compute, no context dependency; worst case minutes only at the 1024-band cap); namespace pollution accepted (bounded per-operator payload keys); >256 skip is reported to callers |

## Phase 7 follow-up findings (main-agent, MALLOC_CHECK_ + control experiment)

| ID | Sev | Area | Finding | Disposition | Evidence |
|---|---|---|---|---|---|
| M-1 | P1 | tests | test_spectral_pipeline MNF E2E allocated 6 floats for a 6x5 readBandData buffer — heap overrun surfaced as "malloc(): unaligned tcache chunk" (crash in the next heavy malloc, QCryptographicHash/OpenSSL — classic deferred-manifestation) | fixed | row0 buffer sized 6*5; suite green 188 assertions; found via MALLOC_CHECK_=3 ("free(): invalid next size") |
| M-2 | P2 | tests | MNF E2E subset-refusal case pointed `input` at the 4-band raster instead of the 2-band one, so the P2-1 refusal never fired (test passed vacuously BEFORE the assertion was exercised) | fixed | input re-pointed to components2.tif; refusal asserts |
| OUT-1 | P1 (out of scope) | fused chain | test_fused_chain "fused NDVI→threshold bit-identical" case SIGSEGVs deterministically (3/3, exit 139): ChangeDetection::changeMask unconditional SICNU_LOG_INFO -> QgsApplication::members() -> QgsSettings -> QLibraryInfo -> applicationFilePath crashes under QCoreApplication-only fixtures on the GCC-16 host | recorded, NOT fixed (out of scope) | CONTROL EXPERIMENT: pristine master (main/build-dev, zero track changes) reproduces identically (exit 139, 3\|2 passed\|1 failed). Files in the crash chain (change_detection.cpp, rs_change_streaming.cpp, test_fused_chain.cpp) are untouched by this track. Belongs to the execution/change-detection families. |

## Verdicts (subagent A, main-agent concurrence)

1. Inverse-MNF roundtrip identity: HOLDS (UsUsᵀ=I composition; 1e-5 relative pinned incl. 256 bands).
2. Digest canonicalization (%.9g round-trip-exact): HOLDS; locale caveat noted — a locale mismatch fails closed (spurious refusal), never silent acceptance.

## Process lesson (recorded)

Rounds 1-4 churned because CMakeLists/header edits landed while a build ran
(stale build.make link failures). Later rounds: edit-then-build discipline.
