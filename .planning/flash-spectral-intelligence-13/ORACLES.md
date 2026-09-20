# ORACLES — flash-spectral-intelligence-13

Each oracle is executable and falsifiable. "Two consecutive green passes" applies to
the whole targeted set at the end (and after review fixes).

## O1 — TCIMF (kernel `tests/test_spectral_tcimf.cpp`)

- O1.1 Independent reference: hand-computable 2×2 and 3×3 cases.
  R = diag(4,1), t = (1,1), S = {(1,−1)}: closed-form w computed by hand in the test
  comment and asserted to 1e-12; both constraints asserted exactly:
  wᵀt == 1 (1e-12) and wᵀs == 0 (1e-12).
- O1.2 **Empty interference degenerates to CEM**: with S = ∅ the TCIMF filter equals
  `SpectralCem::buildFilter` on the same correlation/loading bit-for-bit (or 1e-12),
  and the target scores exactly 1.
- O1.3 Rank refusals (typed, non-crashing):
  - interference matrix with linearly dependent columns (duplicate / zero column /
    collinear under R⁻¹ metric) → `buildFilter` returns false with a named reason;
  - target inside the interference span (projected numerator ≈ 0) → false;
  - singular loaded correlation → false;
  - under-sampled scene → operator-level `InvalidInputData` (2B+2 / B+1 with loading,
    same floor as CEM).
- O1.4 Loading escape hatch: with loading > 0 the reduced floor applies and the
  filter builds (closed form asserted).
- O1.5 Streaming equivalence: operator `rs:tcimf_detection` single-tile bit-exact vs
  in-memory kernel oracle; multi-tile 300×300 within the same 2% gate as MF/ACE/CEM
  (existing harness in test_spectral_detection_streaming.cpp).
- O1.6 Score semantics: target scores exactly 1; interference spectra score exactly 0;
  signed score; NaN for non-finite bands.

## O2 — OSP (kernel `tests/test_spectral_osp.cpp`)

- O2.1 Independent reference: hand-computed projector.
  B = 2, d = (1,0), U = span{(1,1)/√2}: P = I − uuᵀ = [[1/2,−1/2],[−1/2,1/2]],
  w = Pd = (1/2,−1/2); score(x) = wᵀx asserted to 1e-12 for several x; also a 3×3
  case with two interference signatures (projector asserted entrywise).
- O2.2 Projector properties: P² = P and Pd = w (idempotence, asserted to 1e-12);
  score of any interference signature is exactly 0 (1e-12).
- O2.3 Refusals: rank-deficient interference matrix (UᵀU singular: duplicate, zero,
  collinear columns) → typed false; target fully inside the interference subspace
  (w ≈ 0) → typed false; non-finite input → false. Not "plausible-looking" output.
- O2.4 Conditioning diagnostic: result JSON reports the interference-matrix
  conditioning proxy (λmax·k/tr) and the projected target norm; a deliberately
  near-collinear interference pair produces a diagnostic above a documented bound.
- O2.5 Streaming equivalence: operator `rs:osp_detection` single-tile bit-exact vs
  kernel; multi-tile 300×300 within 2% gate.
- O2.6 Scale behavior documented and tested: OSP score scales with |d| (not
  scale-invariant like ACE/CEM) — asserted explicitly so the semantics are pinned.

## O3 — Independent background raster (`tests/test_spectral_background_raster.cpp`)

- O3.1 **Equivalence oracle**: a scene whose background statistics are computable
  from the scene itself, and an independent background raster built from the same
  distribution, produce identical filters/scores (constructed-equivalence: the
  background raster contains exactly the scene's valid pixels, so its accumulated
  statistics equal the scene's — bit-exact modulo accumulation order, asserted at
  1e-9).
- O3.2 Spectral-only compatibility: background raster with a *different wavelength
  grid* (declared WAVELENGTH metadata) is accepted; its spectra are resampled onto
  the scene grid via the shared `SpectralResampling` kernels; result JSON reports
  `backgroundResampled: true` and the provenance.
- O3.3 Typed refusals: band-count mismatch; disjoint wavelength coverage
  (BandCoverage None); all-NoData background; background grid metadata malformed;
  background file missing.
- O3.4 Per-pixel association NOT required: background raster with a different
  CRS/size is accepted when only spectral statistics are used — asserted via a
  deliberately different-size background raster producing valid results; the
  restriction (shared grid required for per-pixel use) is documented.
- O3.5 Diagnostics traceability: `backgroundSource` ("scene" | path),
  `backgroundSamples`, `backgroundCondition`, `backgroundResampled` in result JSON;
  provenance path is the exact input path string.

## O4 — Edge-preserving fusion (`tests/test_spectral_spatial_fusion.cpp` extended)

- O4.1 Edge fixture: a plane with two constant regions separated by a step edge and
  NoData holes; the mean window smears the step (baseline documented), while
  `method: "bilateral"` with a range sigma below the step height keeps the edge
  sharp: pixels adjacent to the edge differ from the mean-window result by more than
  a documented margin, and each region's interior stays exactly at its constant
  (asserted to 1e-6).
- O4.2 Constant-plane invariance: bilateral fusion of a constant valid plane returns
  the plane exactly (1e-6), including at borders.
- O4.3 NoData leak-proofness: invalid pixels are NaN out, never enter a neighbor's
  weighted mean, and valid pixels renormalize (no zero-fill bias) — same contract as
  the mean method, asserted with an explicit-mask variant.
- O4.4 Identities: beta = 0 and radius = 0 are exact pass-throughs for both methods.
- O4.5 Halo equivalence: full-frame vs per-tile-with-r-halo interior **bit-exact**
  for bilateral too (pure window function), for both methods, on a fixture with an
  edge and holes.
- O4.6 Refusals: sigmaRange <= 0 or non-finite → typed error; radius bounds
  unchanged [0,128]; unknown method → typed error.
- O4.7 Operator E2E: `rs:spectral_spatial_fuse` with `method: "bilateral"` writes a
  raster whose interior matches the kernel oracle bit-exactly; result JSON reports
  the method and diagnostics; default (no method) stays byte-compatible with 12.0.

## O5 — Library consolidation (`tests/test_spectral_library*.cpp`)

- O5.1 Single authority: `src/core/spectral_library.*` deleted; the former D13 test
  sections rewire onto the authority API and still pass (SAM ground truth from
  orthogonal geometry, topK truncation, JSON round-trip, Gaussian-SRF constant
  preservation — each re-expressed on the authority, or kept as data-level tests
  where the authority already covers them).
- O5.2 Round-trip: unified library JSON round-trips metadata and wavelength units
  without loss (12 provenance fields compared field-by-field, existing test
  extended with units assertions).
- O5.3 Scale: 10k-entry library query via MatchIndex: ranking and values identical
  to brute-force `matchSpectrum` (bit-exact prefix), and `resamplesPerformed`
  equals the number of distinct grids (≪ N) — no per-query full resampling.
- O5.4 Single similarity kernel: `findNearDuplicates` results unchanged (existing
  test) after rewiring onto the shared SAM kernel; ranking identical to the old
  inline implementation on the same fixture (pinned by the existing suite).
- O5.5 Discovery: agent tool and operators resolve the library through
  `resolveRuntimeDataPath` (no hardcoded relative path); existing tests green.

## O6 — Registration / drift gates (repo generators, not hand-edited)

- O6.1 `tests/test_algorithm_meta_drift` green (pin bumped, sidecars regenerated via
  `--export-catalog`).
- O6.2 `tests/test_capability_drift`, `tests/test_capability_surface_parity`,
  `tests/test_capability_completeness` green — including the two sidecars master is
  missing (rs:cem_detection, rs:spectral_spatial_fuse) plus the new operators.
- O6.3 `tests/test_scientific_contracts`, `tests/test_contract_census_11`,
  `tests/test_contract_cross_surface_11` green (census + graph regenerated via
  `contract_inventory`).
- O6.4 `pi/knowledge/capability-hyperspectral.md` regenerated via `gen-pages`.

## O7 — Gate potency (anti-vacuous-proof)

- Mutation: (a) TCIMF interference projection removed (w = R⁻¹t/tᵀR⁻¹t) → O1.1
  constraint wᵀs == 0 must FAIL; (b) OSP projector replaced by identity → O2.1/O2.2
  must FAIL; (c) bilateral range kernel replaced by constant weight (degenerates to
  mean) → O4.1 must FAIL; (d) background resampling skipped (raw spectra accumulated)
  → O3.2 must FAIL; (e) MatchIndex bucketing removed → O5.3 resamplesPerformed must
  FAIL. Each mutation recorded with the exact failing assertion.

## O8 — Global oracle

Two consecutive green passes of the full targeted set (all spectral suites +
registration gates), `git diff --check` clean, overlap scan vs latest
origin/master and all open PR heads, review P0=0/P1=0 (two review passes), PR
created base=master, no merge, no CI wait.
