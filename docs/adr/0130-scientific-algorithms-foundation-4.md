# ADR 0130: Scientific Algorithms & Processing Foundation 4.0

- Status: Accepted (2026-09-06)
- Scope: `src/processing/algorithms/**`, `src/operators/rs/**`, algorithm tests and documentation
- Companion policy pages: `docs/processing/validation-policy.md`,
  `docs/processing/nodata-and-statistics.md`,
  `docs/processing/grid-and-radiometric-policy.md`,
  `docs/processing/temporal.md`

## Context

The `rs:` family had grown to 82 registered operators across 14 families, but
shared scientific semantics were enforced by convention rather than by code:

- The declared-NoData resolution idiom was re-implemented ~20× with real
  divergences (a 1e-4 epsilon in `rs:kmeans` that silently dropped legitimate
  near-sentinel values; an inert `-9999` default behind a `hasNoData` flag
  that suggested undeclared bands were filtered when they were not).
- Variance conventions were contradictory across streaming statistics
  (population vs sample, undocumented per site).
- Grid compatibility checks were bypassed in places (dNBR's dims-only check
  silently combined CRS-mismatched pre/post pairs; SAR calibration accepted
  grid-incompatible incidence rasters).
- Kernel-level math existed in parallel copies (streaming CVA loop vs
  `ChangeDetection::cvaMagnitude`; four histogram builders with three
  percentile definitions and different binning).
- Alias operators (`rs:ndvi`… ) inherited the default `tolerance` determinism
  grade although their facade declares `bit-exact` (ADR 0124) for the same
  kernel.
- Issue #759: `rs:temporal_breakpoints` understated RMSE whenever the series
  contained NaN gaps (denominator counted index spans, numerator counted
  valid samples); an all-NaN series reported RMSE 0.0.
- Foundational coverage gaps: non-parametric trend (Mann-Kendall/Sen) was
  absent; `rs:threshold_raster` had no numeric test; SAR kernels had no NaN
  contract tests; the streaming endmember operator's "identical to the
  kernel" claim was comment-enforced only.

## Decision

1. **One authoritative primitive per shared scientific semantic**, each
   deleting duplicated code or making an invariant enforceable:
   - `processing/algorithms/nodata_utils.h` — sentinel resolution + exact
     float-cast validity (no epsilons), adopted across the SAR family,
     SAR calibration, and kmeans.
   - `ChangeDetection::cvaMagnitudeBip` — the BIP-layout CVA variant under
     the same kernel owner as the per-band form.
   - `ChangeDetection::histogramBin` — the one fixed-range binning convention
     for every histogram consumer.
2. **Typed grid refusals** at every multi-input combination point that
   previously under-checked (dNBR, SAR incidence), reusing the ADR 0066/0098
   service — no new grid logic.
3. **Determinism-grade truthfulness**: index alias operators declare
   `bit-exact` to match their facade and kernel (ADR 0124 grades are part of
   the API contract).
4. **Issue #759 fixed at the kernel** with `BreakpointResult::validCount`
   exposed; zero-valid fits report NaN (undefined), never 0.
5. **Non-parametric trend as a first-class operator**:
   `rs:temporal_sen_trend` (Sen slope + tie-corrected Mann-Kendall,
   Gilbert 1987), following the temporal family's series-gathering pattern
   (2 GiB guard, TemporalOutputGuard, shared preflight/band roles). The OLS
   `rs:temporal_trend` contract is untouched.
6. **Validation framework as documented policy** (docs/processing/): three
   tolerance grades (exact / bit-exact float / tolerance) tied to ADR 0124,
   the ten-fixture taxonomy per family, the valid-observation denominator
   rule ("a denominator counts observations that contributed to the
   numerator"), and the sample-vs-population variance table.
7. **Deliberately NOT done** (recorded to prevent relitigating):
   - The full-frame speckle filters in `image_enhancement.cpp` stay: they are
     the bit-exact reference for the streaming tile kernels (parity test),
     not dead code.
   - The streaming endmember PPI stays in the operator layer (bounded-memory
     variant, kernel-matrix API would regress the memory policy); the
     identical-RNG contract is now pinned by a test instead of a comment.
   - The `max|v| > 5` EVI/SAVI heuristic stays as the documented fallback for
     undeclared rasters; declared `SICNU_NUMERIC_SCALE` metadata already wins
     at the operator seam (#680).

## Consequences

- New shared-primitive additions cannot drift: the duplicated sites compile
  against the same header, and the agreement tests fail on divergence.
- Output-visible behavior changes are confined to previously erroneous cases:
  kmeans no longer drops near-sentinel values, dNBR/SAR refuse
  grid-incompatible inputs instead of producing wrong numbers, breakpoint
  RMSE is larger (correct) for gapped series and NaN for empty ones.
- `rs:temporal_sen_trend` adds a registered operator ID (83 total); registry
  export is reproducible via the existing dual-registration seam.
- Documentation debt on scientific semantics is retired into four policy
  pages that the review lens "docs-vs-code claims" audits against.

## Verification

- `tests/test_temporal_fit.cpp` — #759 hand-derived regression (√3 case),
  all-NaN → NaN, gapped-perfect-line → 0; Sen/MK hand-derived cases.
- `tests/test_nodata_utils.cpp` — sentinel/NaN/exact-match policy.
- `tests/test_change_detection.cpp` — BIP equivalence + NaN propagation,
  bin-convention edges, dNBR grid refusal, threshold known answers.
- `tests/test_sar_kernels.cpp` — IEEE NaN/domain-edge contracts.
- `tests/test_sar_operators.cpp` — grid-incompatible incidence-raster refusal
  (one-sided CRS → typed failure, no output left behind).
- `tests/test_endmember_extraction.cpp` — streaming-vs-kernel agreement.
- `tests/test_temporal_algorithms.cpp` — `rs:temporal_sen_trend` E2E.
