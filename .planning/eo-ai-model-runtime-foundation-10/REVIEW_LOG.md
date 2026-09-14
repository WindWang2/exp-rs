# REVIEW_LOG — eo-ai-model-runtime-foundation-10

Phase 7: two read-only subagents (A: architecture + EO/scientific correctness;
B: concurrency + lifecycle + test credibility + security) reviewed
`7d78059d1a..991a0c9bfa`. All findings verified against the code before
disposition; fixes re-verified by local test runs (see PERFORMANCE.md numbers).

## Fixed (P0/P1)

- **P0 NMS empty-input crash** (B): `cellPitch` indexed an empty vector and
  `runNonMaxSuppression` called it unconditionally — a detection run with ZERO
  boxes past the confidence gate (normal!) segfaulted at the production call
  site. Fixed: empty early-returns in `runNonMaxSuppression` and
  `dedupDetections` + pitch guard. Regression test added (empty NMS/dedup,
  both overloads).
- **P1 morphology open/close seam inexactness** (A+B): op1 ran on the inner
  (radius-halo) block, so op2 consumed truncated values at core edge rows —
  up to 2r wrong rows at every row-band boundary. Fixed: BOTH ops run over
  the full 2r-halo window; only never-written window-edge rows are inexact.
- **P1 rs:change feed naming broke positional binding** (A): the adapter set
  `feed.name = "inputA"/"inputB"`, which binds BY NAME and refuses models
  whose manifest inputs are named differently (its own metadata promised
  positional). Fixed: feed names stay empty (positional). Test now drives
  `RsChangeOperator::run` (the shipped adapter) instead of a hand-built
  request.
- **P1 wavelength preflight verified wrong physical bands under explicit band
  lists** (A): `band_roles[i]` was mapped to raster band i+1, ignoring the
  `bands` parameter. Fixed: `enforceEoPreflight(model, path, fedBands, feed)`
  now takes the effective selection and the BOUND feed contract; call sites
  run after band resolution (single/classification) and after the
  by-name/positional feed binding (multi-input).
- **P1 rs:classify silently ignored clamp/pad** (A): runSceneClassification
  now refuses `preprocess.pad / clamp_min / clamp_max` with the same wording
  as the tiled engine (#646 discipline).
- **P1 calibration silently skipped on the feather path while provenance
  claimed it** (A): the engine now refuses `calibration_temperature` together
  with feather blending (typed), so an uncalibrated product can never claim
  calibration.
- **P1 drift-test assertion absorbed into a comment** (A): my earlier edit
  merged the catalog-count REQUIRE into the comment line; restored to its own
  line (baseline 32).

## Fixed (P2)

- Calibration_temperature with probability-format output or logit/distance
  head semantics: refused at parse (pow over logits is NaN/meaningless;
  probability stack never passes the collapse) — would have been a silent
  no-op with false provenance.
- Morphology pass: dataset metadata (palette/class-names) now copied to the
  published stage, creation options pinned (TILED+LZW), kernel capped to 65
  at parse, cancellation polled per row band, per-class counts re-tallied
  from the PUBLISHED product (pre-morphology tallies misdescribed it).
- TRT provider: version-guarded API use (destroy() only < TRT 10; enqueueV3/
  setTensorAddress/getTensorShape on >= 10), binding-count + static-shape
  checks, cudaMalloc failure paths free, readback checked.
- OpenVINO provider: input element-count equality check before memcpy,
  output element-type must be f32, output copy bounded by the cv::Mat size.
- runSceneClassification: band-aware scene sample bound (extent × bands) in
  addition to the extent bound; previous-artifact `.prev~` backup + restore
  on publish failure (same contract as the raster engines).
- NMS decode: non-finite geometry dropped before the sort (NaN propagates
  through clamp; inconsistent comparator = UB).
- Test credibility: calibration test now pins three regimes at one threshold
  (raw 0.9 on / sharpened 0.81 off / softened 0.949 on at 0.82 — vacuous if
  calibration is ignored); NMS cancel test flips LATE and asserts strictly
  mid-scan abort, plus an explicit pre-scan-cancel case; the plugin-seam test
  now registers a LATE provider and acquires through it (was order-dependent
  and vacuous); absence test asserts the typed per-provider unavailability
  reasons.

## Accepted debt / follow-ups (dispositioned, not fixed)

- **P2 TRT TU has never been compiled** (no TRT on this host): the adapter is
  capability-gated and version-guarded, but real-TRT compilation is a
  deployment-host follow-up; absence path is typed and tested. ACCEPTED with
  reason (optional provider, default build unaffected, honest not-executed
  status recorded).
- **P3 NaN/extreme geometry hardening beyond the decode drop**: the decode
  now drops non-finite boxes; defensive filtering inside the public
  `nonMaxSuppression` itself is left as hardening (out-of-range double→int
  via extreme FINITE coords remains theoretically reachable through the
  public API). ACCEPTED: production path is decode-gated.
- **P3 radiometric-state vocabulary duplicated as text** in
  `ModelEoDomainContract::validate()`: an include-level anchor to
  satellite_products.h would couple operators/ to processing/algorithms;
  kept textual with the vocabulary documented in both headers.
- **P2 morphology+calibration on multi-input**: unreachable (multi-input is
  probability-stack-only) — no action.
