# ADR 0165 — Spectral Intelligence 12.0: CEM detection, spectral-spatial fusion, library scale, coverage QA

## Status

Proposed (flash-hyperspectral-12)

## Context

Master (baseline `adf8f989`) ships the Spectral 11.0 platform: global and
local dual-window RX, OLS/FCLS/sparse unmixing, SAM/SID plus the hybrid
similarity, matched filter and ACE target detection, streaming MNF, PPI and
endmember analysis, the spectral-table artifact and the wavelength-aware
library seam (ADR 0076–0082, 0092, 0148, 0163). The 12.0 baseline audit
identified five capability gaps that this track closes:

1. **Target detection lacks CEM.** MF and ACE cover the signed projection and
   the squared whitened cosine; the constrained energy minimization detector
   — the third standard member of the family, tolerant of multiplicative
   brightness scaling — is absent. Detection operators also surface no
   background diagnostics (sample count, conditioning), and the MF/ACE
   background covariance has no explicit low-sample policy (a tiny ridge
   silently masks rank-deficient scenes).
2. **No spectral-spatial layer.** Per-pixel detection scores are consumed
   directly; isolated single-pixel false alarms have no documented,
   NoData-correct spatial consistency refinement.
3. **Library matching does not scale.** `matchSpectrum` re-resamples the
   query spectrum once per mismatched-grid entry and fully scores (SAM+SID)
   and globally sorts every entry — O(N·(R+B)) per query, hostile to
   pixel-wise search over thousand-entry libraries.
4. **Resampling coverage is all-or-nothing.** Out-of-range target bands
   yield NaN (or a typed refusal) without a per-band coverage description,
   so callers cannot quantify how much of a target grid the source covers.

## Decision

Two new operators and three kernel extensions, all additive; registration
surfaces receive append-only edits.

### A. CEM target detection (`SpectralCem`, `rs:cem_detection`)

* Standard R&C form against the scene CORRELATION (second-moment) matrix:
  `w = R⁻¹t/(tᵀR⁻¹t)`, `score(x) = wᵀx`; the target scores exactly 1. Two
  streaming passes (correlation, score) over the shared valid-pixel
  predicate.
* Regularization: scaled diagonal loading `loading·(tr(R)/B)·I`, default 0 —
  the same strategy as local RX (ADR 0163), predictable bias.
* Fail-closed: scenes with fewer valid background samples than
  `minSamplesRequired` (2B+2; B+1 with `loading > 0`) are refused with a
  typed error instead of letting a ridge mask a rank-deficient estimate.
  Singular loaded matrices, degenerate targets and zero/negative constraint
  denominators are typed refusals.
* Diagnostics for the whole detector family: `backgroundSamples` and
  `backgroundCondition` (deterministic lower bound `λmax·B/tr` via
  `SpectralAnomaly::conditionProxy`, never overstating conditioning) in the
  result JSON of `rs:matched_filter`, `rs:ace` and `rs:cem_detection`.

### B. Spectral-spatial score fusion (`SpectralSpatialFusion`,
`rs:spectral_spatial_fuse`)

* Convex local-consistency fusion:
  `fused = (1−β)·s + β·mean{s(q) : q ∈ W(p), q valid}`, W a (2r+1)² window
  clamped to the raster, the pixel itself a member (isolated valid pixels
  keep their own score — no zero-fill bias).
* Leak-proof NoData: invalid pixels are never fused and never enter a
  neighbor's mean; valid pixels near NoData renormalize over the valid
  members. β=0 or r=0 is an exact pass-through (byte-equivalent claims are
  testable).
* Pure window function → tile-agnostic determinism (a tile computed with an
  r-pixel halo matches the whole-plane interior bit-for-bit).

### C. Library-scale matching (`SpectralLibraryScale::MatchIndex`)

* Grid-bucket index built once per library by EXACT (band count, wavelength
  grid bytes, FWHM bytes) equality — no hashing in the correctness path.
  A query is resampled once per distinct non-query grid (G resamples)
  instead of once per entry; same-band-count entries are scored raw exactly
  as brute force does.
* Prescreened top-K: the prescreen computes the SAME clamped cosine/acos —
  in the same accumulation order — as the SAM kernel, sorts on the exact
  degree key with the (angle, entry index) tie rule, and defers only the SID
  histogram to the kept prefix. Output is bit-identical to the brute-force
  prefix by construction; undefined (sentinel/non-finite/zero-norm) angles
  sort last in library order, matching `matchSpectrum`.
* `Stats{resamplesPerformed, resamplesAvoided, entriesScored,
  entriesPrescreened}` provide machine-independent scale evidence.

### D. Resampling coverage flags (`SpectralResampling::analyzeResamplingCoverage`)

* Per-target-band coverage without touching data: the linear path is binary
  Full/None (exactly where the kernel produces values/NaN); the Gaussian SRF
  path adds Partial when the target center is in range but its response
  reaches past the source edge — captured Gaussian mass < 0.99 via the
  closed-form erf integral.
* `rs:spectral_resample` reports `coverage` (per band), `coverageFull`,
  `coveragePartial`, `coverageNone` in its result JSON.

## Alternatives considered

* TCIMF/OSP detectors — valuable, but CEM is the standard companion the
  track gaps name; TCIMF is a documented future extension on the same
  skeleton. (Known limitations)
* Ledoit–Wolf shrinkage for CEM — same objection as ADR 0163 D2: the oracle
  stops being a closed form. Scaled loading keeps the bias explicit.
* Edge-preserving/morphological spatial fusion — needs structure-element
  choices with contested semantics; the convex window mean is closed-form
  verifiable and streamable. (Known limitations)
* Dot-cosine-only prescreen without the exact acos — ranking by acos of the
  same cosine is bit-identical to brute force; ranking by raw cosine is not
  (FP collapse at the sort key). The extra acos per entry is negligible.
* Independent background raster for detection — explicitly out of scope this
  track (existing limitation retained).

## Consequences

* algorithm_meta catalog 43 → 45 sidecars (rs:cem_detection,
  rs:spectral_spatial_fuse); drift pins updated in the same change and the
  shipped sidecars regenerated via `export-catalog`.
* capability catalog/contract/agent JSON rows appended for both operators;
  both declare the `target-detection` task and `hyperspectral` capability
  group.
* Existing operators/rasters stay untouched; `rs:matched_filter`/`rs:ace`
  gain only additive result-JSON diagnostics.
