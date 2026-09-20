# Processing: SAR Radiometric Domain & Geometry Extension (Foundation 5.0)

> Authority for the SAR-specific numeric-domain and geometry contracts added
> by Scientific Algorithm Foundation 5.0 (Milestone D). Companions:
> [grid-and-radiometric-policy.md](grid-and-radiometric-policy.md),
> [validation-policy.md](validation-policy.md). Baseline SAR metadata keys
> and domain math live in `src/processing/algorithms/sar/sar_metadata.h`.

## 1. Dual-pol features (`rs:sar_dualpol_features`)

1. Kernels compute on **linear power**. The declared `SICNU_SAR_DOMAIN`
   (`linear_power` | `db`) wins; the explicit `domain` parameter overrides;
   undeclared inputs fall back to linear with a logged warning. dB inputs are
   converted (10^(dB/10)) **before** the kernel, so the output domain never
   depends on the input domain.
2. Features: `ratio` (VV/VH), `normalized_difference`, `log_ratio`
   (10·log10(VV/VH)), `rvi` = 4·VH/(VV+VH), `span` = VV+VH.
3. **Honesty rule**: `rvi` is the dual-pol Sentinel-1 APPROXIMATION. It is
   not the quad-pol RVI (4σ/(σVV+σVH+2σHV)) — the platform models no
   second cross-pol channel, and no doc/operator may claim quad-pol
   capability.
4. Nonpositive linear-power values are outside the domain: NaN, never
   clamped (the per-kernel NoData policy of sar_metadata.h).

## 2. Terrain geometry under the declared constant-geometry contract
   (`rs:sar_terrain_masks`)

1. Products: local incidence angle (surface-normal form) and the classic
   geometric layover/shadow classes (α > θi layover; α < θi − 90° shadow;
   α = signed range-direction slope toward the sensor). Geometry is defined
   in `sar/sar_terrain_geometry.h` and pinned by closed-form tests.
2. This is the rigorous **executable subset** for scenes that declare only a
   constant incidence angle and look geometry. It is not range-Doppler
   simulation: no layover displacement, no radiometric terrain correction
   beyond the exposed cos θi / cos θl factor, no per-pixel orbit geometry.
3. **Heading vs look azimuth (#785, Foundation 6.0)**: the flight heading
   (`SICNU_SAR_HEADING_DEG`, platform direction of travel) and the antenna
   look azimuth (`SICNU_SAR_LOOK_AZIMUTH_DEG`, the boresight ground azimuth
   the terrain geometry actually consumes) are orthogonal. `lookDirection:
   right|left` derives `look = heading + 90° (right)` or
   `heading − 90° (left)`; an explicit `lookAzimuthDeg` parameter overrides.
   Every terrain operator reports the effective look azimuth. Feeding the
   heading itself (the pre-6.0 behavior) was a 90° orthogonal error that
   invalidated masks and radiometric flattening; results for unchanged
   parameter sets therefore change by design in 6.0.

## 3. Orbit geometry: the declared-contract executable subset

Full range-Doppler terrain correction / geocoding needs orbit state vectors
and sensor timing (zero-Doppler range equations, DEM sampling in range time,
azimuth compression timing). Scenes that do not declare them keep the
constant-geometry subset — **requests are typed refusals, never
approximations**. The additive extension contract (dataset metadata):

```
SICNU_SAR_ORBIT_STATES       "t;x;y;z;vx;vy;vz|..." (UTC seconds on the
                             scene azimuth epoch; WGS84 ECEF metres, m/s)
SICNU_SAR_AZIMUTH_START_UTC  azimuth time of image row 0 (s, same time base)
SICNU_SAR_PRF                pulse repetition frequency (Hz)
SICNU_SAR_RANGE_WINDOW       slant-range gate "start;stop" (s, two-way)
SICNU_SAR_RANGE_RATE         range sampling rate (samples/s)
```

**Scientific Algorithms 7.0 consumes this contract** behind
`sar/sar_orbit.h` (parser + segment validation, Hermite interpolation,
zero-Doppler geolocation, forward range-Doppler, per-point incidence — all
known-answer tested against a synthetic circular orbit):

- `rs:sar_terrain_masks product=local_incidence_orbit` geolocates every
  pixel center (row → azimuth time via PRF, column → slant range via
  RANGE_RATE and the two-way light path, DEM height above the ellipsoid)
  and writes the incidence angle from the REAL line of sight — range- and
  height-dependent, unlike the constant-geometry product. Pixels whose
  (azimuth, range, height) cannot resolve on the shell (e.g. ranges
  shorter than the nadir distance) are NoData, never fabricated.
  Ranges: sample spacing = c / (2 · RANGE_RATE) metres.
- Declared-but-invalid segments (unsorted, non-finite, single state,
  window/grid contradictions beyond a 2-sample tolerance) are typed
  refusals.
- What this is NOT: full range-Doppler terrain correction still requires
  DEM resampling into range time and output-grid geocoding with
  radiometric rescaling — no operator claims it. Radiometric terrain
  flattening remains the plane-fit / projected-area model of sar_terrain.h,
  honestly named `gamma0_rtc`.

## 4. Forward Range-Doppler geocoding (`rs:sar_geocode`, Scientific Processing 8.0)

The 7.0 backward product geolocated SAR pixels; 8.0 closes the chain: with
the SAME declared contract (§3), `rs:sar_geocode` maps every cell of a DEM
map grid through forward range-Doppler (ground → zero-Doppler azimuth time +
slant range → source row/column via PRF and the range gate) and writes the
geocoded products. Authority: `processing/algorithms/sar/sar_geocoding.h`
(scene-contract parser shared in spirit with the backward path, per-cell
geometry, bounded bilinear/nearest samplers); streaming driver:
`operators/rs/rs_sar_geocode_operator.cpp`.

1. **Grid contract**: the DEM defines the output grid (CRS + north-up
   geotransform required; rotated grids are typed refusals — the
   terrain-family north-up convention). Geographic grids geolocate by the
   geotransform; projected grids transform every pixel center through the
   foundation CRS policy (`geospatial/crs`).
2. **Products** (fixed five-band Float32, order in
   `SICNU_SAR_GEOCODE_BANDS`): `backscatter` (resampled radiometry in the
   input's own declared domain — pass calibrated sigma0), `gamma0`
   (= backscatter · sin θ0 / sin θL, Ulander 1996 area factor from REAL
   per-pixel geometry — distinct from the constant-geometry plane-fit model
   of `rs:sar_terrain_flatten`), `incidence` (ellipsoid reference θ0),
   `local_incidence` (terrain facet θL), `layover_shadow`
   (0 normal / 1 layover / 2 shadow, NaN NoData).
3. **Real-geometry classes**: layover/shadow on the map grid use the
   per-pixel LOS elevation θe = asin(ŝ·û) and the slope α along the
   BEAM-TRAVEL horizontal direction (away from the sensor — the direction
   slant-range monotonicity is measured in): LAYOVER when α > 90° − θe;
   SHADOW when α < −θe. These reduce exactly to the §2 constant-geometry
   conditions when the LOS is constant.
4. **Contract lookup**: the orbit contract is read from the SAR product's
   own metadata first, with the DEM (the co-registered scene carrier, as
   the backward product reads it) as fallback — one effective contract per
   run either way.
5. **Honesty rule (unchanged)**: cells whose forward geometry does not
   resolve (no zero-Doppler crossing inside the declared segment), whose
   source position falls outside the SAR raster, whose DEM height is
   NoData, or whose resampling taps are non-finite are NaN — counted per
   cause in the result (`unresolvedGeometryPixels`, `outsideImagePixels`,
   `demNoDataPixels`, `sourceNoDataPixels`), never fabricated. Undeclared
   or contradictory orbit contracts are typed refusals.
6. **Memory**: O(tile + bounded source window); windows above a fixed
   budget are never materialized (per-pixel bounded reads instead).
   Determinism: bit-exact grade (pure per-pixel math, no parallel
   reductions).
7. **Evidence**: `tests/test_sar_geocoding.cpp` — analytic circular-orbit
   known answers (row/col/incidence/factor/class), backward∘forward
   round-trip closure < 1 mm, independent-vector-math facet validation,
   sampler NaN/bounds matrix, operator E2E round-trip + layover/RTC
   fixture, refusal matrix.

## 5. Multi-date SAR statistics (`rs:sar_temporal_stats`, Scientific Processing 8.0)

Where `rs:sar_ratio`/`rs:sar_change` cover the bi-temporal case, 8.0 adds
the N-scene summary. Authority: `processing/algorithms/sar/sar_temporal.h`.

1. **Domain rule (unchanged)**: kernels compute in LINEAR POWER; dB-declared
   scenes are converted once (`SICNU_SAR_DOMAIN` wins, explicit
   `inputDomain` overrides, undeclared falls back to linear). Mixed
   declarations inside a stack are typed refusals — statistics over a
   silently mixed stack would be meaningless.
2. **Valid sample**: finite AND strictly positive (nonpositive power is
   outside the physical domain — NaN, never clamped).
3. **Robust change**: the per-pixel baseline is the median linear-power
   sample (upper-median selection for even counts, documented); deviations
   are |10·log10(x) − baseline| dB — the log domain makes multiplicative
   speckle additive, so baseline and deviations are speckle-robust. Each
   scene's DECLARED band sentinel is also excluded before the aggregates.
4. **Products** (fixed band order, `SICNU_SAR_TEMPORAL_BANDS`): mean_db /
   mean_linear / std_dev_linear / cv / min_db / max_db / argmax_date /
   baseline_db / max_log_deviation_db / changed_dates / valid_count.
   Pixels under `minValid` observations are NaN everywhere except
   valid_count.
5. **Evidence**: `tests/test_sar_temporal_stats.cpp` (closed forms on
   hand-computed series, threshold counting, invalid-sample bookkeeping,
   domain conversion, refusal matrix).

## 6. Complex (SLC) raster artifact (`sar/sar_complex.h`, Advanced SAR 10.0)

1. One SLC channel = one **CFloat32** GDAL band (complex scattering
   amplitude). Complex kernels never pass through the float BIP streams:
   GDAL's complex→float conversion keeps only the real part, which would
   silently destroy the phase. Reads go through
   `readBandWindowNative` (native CFloat32), writes through
   `GdalStreamingOutput::writeTileRaw(..., GDT_CFloat32)`.
2. Channel identity contract (additive dataset metadata):
   `SICNU_SAR_COMPLEX_CHANNELS` = "HH;HV;VV" (reciprocity: SHV = SVH) or
   "HH;HV;VH;VV" — **token order is band order**. Operators additionally
   accept explicit band-mapping parameters (`hhBand`/`hvBand`/`vhBand`/
   `vvBand`); an explicit mapping wins. A declared-but-missing channel is a
   typed refusal (`POLARIZATION_MISMATCH`), never a guessed band.
3. Invalid samples — non-finite components or the componentwise declared
   NoData sentinel pair — are normalized to (NaN, NaN) by the tile stream
   (`ComplexBandTileStream`), so kernels never see sentinels (the
   sar_metadata.h per-kernel policy, generalized componentwise).
4. Memory: the complex tile stream materializes O(tile + halo) per step;
   halo regions are edge-replicated (the GdalBlockStream contract). Full
   complex planes are never created by streaming kernels.
5. Evidence: `tests/test_sar_complex.cpp` (round-trip, halo replication,
   sentinel normalization, detected-input refusal).

## 7. PolSAR decompositions (`rs:sar_polsar_decompose`, package B)

1. **Mode contract**: full-pol complex channels only (HH/HV/VV under the
   reciprocity contract; a 4-channel declaration with HV≠VH collapses to
   HV when `assumeReciprocity` holds, and this is reported as
   `reciprocityAssumed: true`). Dual-pol detected inputs are refused —
   no dual-pol approximation is relabeled as a quad-pol decomposition.
2. **Ensemble**: single-look covariances are rank 1 (H identically 0,
   anisotropy NaN). The window (`windowSize`, odd, default 5) accumulates
   the local covariance ensemble; the effective-resolution cost is the
   window's.
3. **Model conventions** (defined in `sar_polsar.h`, pinned by
   `test_sar_polsar.cpp`):
   Volume Cv = fv·[[1,0,1/3],[0,2/3,0],[1/3,0,1]] (Freeman-Durden 1998
   random dipoles); double Cd = fd·[[|α|²,0,α],[0,0,0],[ᾱ,0,1]]; surface
   Cs = fs·[[1,0,conj β],[0,0,0],[β,0,|β|²]]; helix Ch = fh·u·u^H with
   u = [1, j, −1]/2 (right helicity; left = conjugate). Branch rule:
   Re(C13 − fv/3) ≥ 0 → surface branch (α = 0), else double branch
   (β = 0). Yamaguchi: helix first (fh = −4·mean(Im C12, Im C23)), then
   volume from the decontaminated cross-pol power (fv = 1.5·(C22 − fh/4)),
   then the shared branch rule. Negative residuals are clamped to 0 —
   documented SPAN break near the noise floor; unclamped cases conserve
   SPAN exactly (test-pinned). The orientation-adapted volume of the
   refined (2012) Yamaguchi variant is NOT implemented.
4. **Products** (fixed band orders, declared as `SICNU_SAR_POLSAR_BANDS`):
   `pauli` → odd_bounce/double_bounce/volume/span;
   `h_alpha` → entropy/anisotropy/alpha_deg/dominance/λ1/λ2/λ3
   (Cloude-Pottier; anisotropy NaN for rank-1 ensembles — honest, not 0);
   `freeman_durden` → surface/double_bounce/volume/span;
   `yamaguchi` → surface/double_bounce/volume/helix/span.
5. **Eigen machinery**: deterministic cyclic Jacobi for 3×3 Hermitian
   matrices (`sar_hermitian3.h`, fixed sweep order, fail-closed convergence
   gate). No new third-party dependency (OpenCV `cv::eigen` is real-
   symmetric only).
6. Evidence: `tests/test_sar_polsar.cpp` (constructed eigen-systems,
   canonical targets, exact model recovery, SPAN conservation),
   `test_sar_platform10.cpp` (registry E2E).

## 8. InSAR base chain (package C)

Honest scope: base interferometry on CO-REGISTERED same-grid complex SLC
pairs. NOT implemented and never claimed: full image co-registration
(translation-model refinement only), topographic phase removal from
DEM/orbit, atmospheric correction, PSI/SBAS time-series analysis.

1. `rs:sar_interferogram` — same-grid preflight (#929 `compareGrids`;
   `GRID_MISMATCH` / `DIMENSION_MISMATCH` refusals) → complex
   interferogram s1·conj(s2) (CFloat32) + optional coherence
   (windowCoherence, [0,1], NaN where no jointly valid pair).
   `flattenRamp` = none|linear|quadratic removes a robust low-order
   polynomial fit of the WRAPPED phase (streaming IQR-clipped least
   squares, `PhaseRampFitter`, O(1) memory) — an honest flat-earth
   approximation, not topographic removal.
2. `rs:sar_phase_filter` — Goldstein-Werner spatial filter
   (Z_f = Σ|z|^α·z / Σ|z|^α; α ∈ [0,1]); output = filtered UNIT phasor as
   a complex raster (the chain stays in the complex artifact domain).
3. `rs:sar_unwrap` — the BUILT-IN reference unwrapper is a deterministic
   quality-guided flood fill (seed = highest-quality valid pixel;
   neighbor-relative Itoh unwrapping; exact when |Δφ| < π). It is NOT
   residue-aware and NOT a global optimum — no SNAPHU/MCF semantics. The
   `provider` parameter is the external seam: any name other than
   "builtin" is refused (`UNWRAP_PROVIDER_UNAVAILABLE`), never silently
   substituted. Single-scale plane unwrapping behind a 2 GiB budget
   (`MEMORY_BUDGET_EXCEEDED` beyond — smaller AOI or external provider).
4. `rs:sar_displacement` — d_los = −λ·φ/(4π) (positive = toward the
   sensor) with λ from `wavelengthUm` or `SICNU_SAR_WAVELENGTH_UM`
   (refusal when missing). The Itoh discontinuity ratio is reported in the
   result; above `warnThreshold` the operator WARNS — it cannot reliably
   detect "wrapped vs unwrapped" from data alone and refuses to pretend.
5. `rs:sar_coregister` — global-translation refinement: magnitude patch
   NCC with parabolic sub-pixel refinement and a median over confident
   patches; the slave is resampled by bilinear complex interpolation.
   Translation only — no affine/DEM-based warp. `reportOnly=1` estimates
   without writing.
6. Evidence: `tests/test_sar_insar.cpp` (kernel closed forms, exact
   unwrap under the Itoh condition, ramp recovery with outlier clipping),
   `test_sar_platform10.cpp` (full chain closure interferogram → filter →
   unwrap → displacement on a synthetic pair).

## 9. Multi-temporal SAR with acquisition-time semantics
   (`rs:sar_temporal_events`, package D; closes ISSUES.md S-1)

1. **Date contract**: the explicit `dates` parameter (ISO 8601 UTC array,
   ascending) wins; per-scene `SICNU_SAR_ACQUISITION_UTC` metadata fills
   the rest. Missing/unparseable/non-ascending dates are typed refusals
   (`ACQUISITION_DATES_MISSING` / `DATES_NOT_ASCENDING`) — this family no
   longer emits index-only temporal products.
2. **Irregular intervals are the normal case**: every time quantity is an
   actual floating-day offset since scene 0 (never an equal-interval
   assumption). Missing acquisitions (sentinel/nonpositive samples) drop
   out of the aggregates without shifting the dates of the remaining
   scenes.
3. **Event kernel** (shared baseline convention with §5: upper-median of
   the linear-power samples): a date is an EVENT when
   |10·log10(x) − 10·log10(median)| ≥ `changeThresholdDb`. Products (fixed
   9-band order, `SICNU_SAR_TEMPORAL_EVENT_BANDS`): event_flag,
   event_count, first/last_event_index (0-based), first/last_event_days
   (days since scene 0), max_deviation_db, argmax_days, valid_count.
   Pixels under `minValid` observations are NaN everywhere except
   valid_count.
4. `rs:sar_temporal_stats` stays byte-compatible (fixed band order per
   §5) and gains an ADDITIVE result-JSON echo: `dates[]` +
   `timeSemantics`, so its index-valued argmax/argmin bands gain calendar
   semantics at the consumer. Without resolvable dates it keeps the
   legacy index-only behavior.
5. Evidence: `tests/test_sar_temporal_events.cpp` (grammar, day
   arithmetic, hand-computed events with holes), `test_sar_platform10.cpp`
   (registry E2E dating an event at day 24; the stats dates echo).

## 10. Pair-level InSAR input truth (`sar/sar_baseline.h`, Advanced InSAR
   11.0, package A)

1. **Versioned scene truth** (`InSarSceneTruth`, `kInSarTruthVersion = 1`):
   absolute acquisition UTC (the `SICNU_SAR_ACQUISITION_UTC` contract,
   §9), radar wavelength in µm (`SICNU_SAR_WAVELENGTH_UM`), the orbit
   segment (§3 `SICNU_SAR_ORBIT_STATES`), and the optional absolute anchor
   of the orbit time base (`azimuthStartUtcSec`). Unset fields are quiet
   NaN — never zero, which is a valid time.
2. **Pair truth** (`buildPairTruth`): validates both scenes, refuses
   wavelength disagreement beyond 1e-9 relative
   (`WAVELENGTH_INCOMPATIBLE` — interferometric phase is only defined
   between co-nominal radars), computes the signed temporal baseline
   (floating days), and — when both absolute anchors exist — refuses
   disjoint orbit windows (`ORBIT_EPOCH_MISMATCH`) because such
   acquisitions cannot share an imaged area.
3. **Per-ground-point baseline** (`pairBaselineAtGround`): zero-Doppler
   crossings of BOTH orbits at the point (§3 authority), sensor
   interpolation at the crossings, LOS ground→master-sensor, then the
   §3 `interferometricBaseline` (B∥/B⊥/|Δr|). No crossing = typed refusal
   (`BASELINE_NO_ZERO_DOPPLER_*`), never a guessed baseline.
   `heightAmbiguityM` = λ·r·sinθ/(2·B⊥) is the honest sensitivity metric.
4. Evidence: `tests/test_sar_baseline.cpp` — analytic equatorial
   translated-orbit closed forms for t₁/t₂/r₁/r₂/B∥/B⊥ derived
   independently of the implementation's numerics.

## 11. Rigorous DEM/orbit topographic phase (`rs:sar_remove_topographic_phase`,
   Advanced InSAR 11.0, package B; DECISIONS D-002)

1. **Chain**: per interferogram pixel — map center → WGS84 geodetic (GDAL/
   OSR authority), DEM height bilinear at that map location, zero-Doppler
   ranges of BOTH orbits at the ground point, φ_topo =
   wrap(−4π(r_master − r_slave)/λ). Removal rotates the complex sample by
   e^{−iφ_topo}: amplitude preserved, φ_residual = wrap(φ_ifg − φ_topo).
   This is the true range-difference phase — NOT the B⊥ approximation
   (degenerate at nadir, needs per-pixel θ) and NOT the §8 flat-earth
   ramp fit.
2. **Sign conventions** (pinned by tests): φ carries a MINUS vs range
   difference (two-way phase); r_master leads (s_master·conj(s_slave)
   convention); swapping master/slave negates φ_topo. Heights are metres
   ABOVE THE WGS84 ELLIPSOID — geoid-attached DEMs must be converted
   upstream (the data cannot declare its vertical datum reliably).
3. **Fail-closed preflights**: wavelength from `wavelengthUm` or
   `SICNU_SAR_WAVELENGTH_UM` else `TOPO_PHASE_METADATA_MISSING`; orbit
   strings parsed and validated (`ORBIT_SEGMENT_INVALID`); interferogram
   CRS/geotransform required (`GRID_CRS_MISSING`); DEM same CRS
   (`DEM_CRS_MISMATCH`), north-up axis-aligned grids only
   (`DEM_GRID_UNSUPPORTED`), full coverage of the interferogram extent
   (`DEM_EXTENT_INSUFFICIENT`); not one computable pixel →
   `TOPO_PHASE_ORBIT_COVERAGE`. Invalid DEM samples stay NaN through the
   product — never interpolated into plausible phases.
4. Streaming: O(tile) memory; two zero-Doppler solves per pixel
   (§3 authority, O(orbit states) each). Cancel-safe via the context
   probe.
5. Evidence: `tests/test_sar_topographic_phase.cpp` (analytic equator
   oracle + independent scan/bisect oracle off-nadir + swap antisymmetry +
   removal/NaN semantics), `test_sar_platform11.cpp` (operator E2E:
   interferogram CONSTRUCTED from the analytic forward model → residual
   < 0.02 rad; no-wavelength and CRS-mismatch refusals).

## 12. Local offset-field co-registration (`rs:sar_coregister_local`,
   Advanced InSAR 11.0, package C; DECISIONS D-003)

1. **Model**: a TRANSLATION FIELD — per-patch magnitude NCC on a lattice
   (parabolic sub-pixel, the §8 confidence rules), 3×3 median over
   confident neighbors for speckle-outlier rejection (disable with
   `medianRadius=0` for deformation gradients stronger than the lattice
   spacing), bilinear interpolation over the lattice node centers, and
   bilinear complex resampling dst(x,y) = src(x + dx, y + dy) — the
   negated application of the content-displacement offsets (the NCC says
   where the slave content came from). No
   affine/polynomial warp, no DEM-based refinement — `rs:sar_coregister`
   remains the global single-shift special case.
2. **Honest coverage**: unconfident patches stay visible (flagged, not
   dropped) and fall back to the global shift model downstream; the
   fallback is counted (`confidentPatches` / `totalPatches`). Flat /
   decorrelated patches have no meaningful NCC peak and must NOT invent
   offsets.
3. Products: aligned slave (CFloat32) + optional 3-band offset field
   (dx, dy, confidence = peakRatio; 0 where unconfident). Both planes
   materialized behind the 2 GiB budget (`MEMORY_BUDGET_EXCEEDED`).
4. Evidence: `tests/test_sar_coregistration.cpp` (piecewise-shift known
   answers ±0.25 px, warp reconstruction, flat-region degradation),
   `test_sar_platform11.cpp` (registry E2E).

## 13. External unwrap providers (`rs:sar_unwrap` `provider` seam, Advanced
   InSAR 11.0, package D; DECISIONS D-004)

1. The BUILT-IN reference (§8.3) is untouched and stays the default.
   `provider=<name>` runs a GENERIC external-executable contract — no
   third-party code, no new dependency, SNAPHU slots in as a process:
   binary discovery = `providerBin` → `SICNU_SAR_UNWRAP_<NAME>_BIN` →
   PATH; `providerArgs` is a command-line template with {input} {output}
   {width} {height}; the tool must write a raw Float32 plane of exactly
   width·height samples.
2. Data contract: the wrapped phase is staged as raw Float32 (NaN pixels
   written as 0.0 and the input validity mask RE-APPLIED to the output —
   masked pixels never adopt the tool's guess); all scratch lives in a
   per-call temporary directory and is removed on EVERY exit path.
3. Typed failures: `UNWRAP_PROVIDER_UNAVAILABLE` (no binary — never a
   silent builtin fallback), `UNWRAP_PROVIDER_FAILED` (non-zero/abnormal
   exit; stderr tail attached), `UNWRAP_PROVIDER_TIMEOUT` (process
   killed), `UNWRAP_PROVIDER_INVALID_OUTPUT` (missing/short/non-finite
   output). Cancellation kills the process and surfaces `CANCELLED`.
4. Evidence: `tests/test_sar_unwrap_provider.cpp` + the deterministic
   fake provider (`tests/support/sar_fake_unwrap_provider.cpp`: ok / crash
   / truncated / NaN / hang modes), `test_sar_platform11.cpp` (registry
   E2E echo + missing-binary refusal).

## 14. Pair networks and small-baseline linear inversion
   (`rs:sar_pair_network`, `rs:sar_network_inversion`, Advanced InSAR 11.0,
   packages E–F; DECISIONS D-005/D-006)

1. **Pair network**: scene truths (§10) sorted by acquisition UTC, paired
   `all_pairs` or `consecutive`, filtered by |Δt| and the |B⊥| screening
   metric (evaluated at the master's orbit mid-time nadir — a graph
   metric, not a per-pixel product). Union-find connectivity + reference
   selection. Fail-closed (Oracle 2): invalid truth / wavelength
   disagreement / disjoint absolute windows are typed refusals; a
   disconnected graph refuses (`PAIR_GRAPH_DISCONNECTED`) unless
   `allowDisconnected=true` returns the component map explicitly. Bounds:
   512 scenes, 65536 pairs. **Phase-closure QA** (`sar_phase_closure`): for three interferograms
   formed from one consistent same-grid SLC stack the wrapped closure
   arg(I_ab·I_bc·I_ca) is identically 0 — a consistency check of the
   PRODUCTS. It does NOT detect deformation (per-scene phase is
   common-mode); a misaligned resampling between the three interferograms
   breaks the identity.
2. **Network inversion**: SBAS-style LINEAR solve of min Σ w_i(d_i −
   (Gu)_i)² per pixel (G = pair-graph incidence), producing per-epoch
   displacement (relative to the reference epoch), OLS linear velocity,
   and the fit RMS residual. Missing pairs drop their row; epoch
   connectivity is per pixel — only the reference component solves, other
   a per-pair STACK property (e.g. mean coherence²); a per-pixel scalar
   weight is mathematically inert and not an input. Distinct missing-data
   patterns are factorized once and cached (≤ `maxPatterns`, else
   `NETWORK_INVERSION_PATTERN_BLOWUP`); rank-deficient systems refuse
   (`NETWORK_INVERSION_RANK_DEFICIENT`), never pseudo-inverted. Bounds:
   64 pairs (the u64 pattern mask), 200 epochs.
3. **Honest scope**: atmospheric phase stays in the epoch displacements;
   no PS selection; no APS separation — this is NOT PSI and no surface
   may present it as PSI.
4. Evidence: `tests/test_sar_pair_network.cpp` (constraint filtering,
   connectivity, per-branch typed refusals), `test_sar_phase_closure.cpp`
   (the algebraic identity, including with per-scene phase injection),
   `test_sar_network_inversion.cpp` (constructed-truth recovery, missing
   data, weights, pattern bounds), `test_sar_platform11.cpp` (registry
   E2E: 1 mm/year velocity recovered exactly; intersect vs perpixel
   semantics).

## 15. Declared calibration state: re-calibration is a typed refusal (SAR 12.0)

1. Every operator that writes a radiometric SAR product also writes the
   dataset-level `SICNU_SAR_CALIBRATION` token (`sigma0` | `gamma0` |
   `beta0` | `dn`; accepted input spellings `sigma_naught`/`sigma`/`gamma`/
   `beta`/`digital_number` normalize onto the canonical tokens — see
   `sar_metadata.h`). Filtering kernels that are radiometrically neutral
   (`rs:sar_speckle` spatial and multitemporal) **propagate** the input's
   declared state onto their output instead of dropping it.
2. `rs:sar_calibrate` applies the DN formula `sigma0 = (DN² − noise)/A²`.
   A product declaring any **calibrated** state (`sigma0`/`gamma0`/`beta0`)
   is a typed refusal (`InvalidParameter`): re-applying the DN formula to
   an already-calibrated product silently double-scales the radiometry.
   A declared `dn` (or an absent declaration) is accepted. A declared token
   the platform cannot interpret is likewise a refusal — never treated as DN.
3. `rs:sar_backscatter` cross-checks `fromCalibration` against the
   declared state before applying any geometry factor. Mismatch = typed
   refusal with the remedy in the message. The pure numeric-domain path
   (`from == to`, dB ↔ linear) applies no geometry, so it is exempt.
4. Rationale: the same fail-closed lineage as the declared-`SICNU_SAR_DOMAIN`
   refusals (§1) — requests that would corrupt radiometry are typed
   refusals, never approximations.
5. Evidence: `tests/test_sar_operators.cpp` (re-calibration refusals for
   every calibrated state, DN acceptance, unrecognized-token refusal,
   fromCalibration mismatch refusal, matching-state conversion, speckle
   state propagation incl. multitemporal reference-scene semantics,
   inert sameState domain conversion).

## 16. Radiometric State 13.0: census, derived products, LUT calibration (Track E)

1. **Census.** Every operator registered under the `rs:sar_` prefix carries a
   machine-readable state rule or an explicit exemption in
   `tests/test_sar_radiometric_state.cpp` (`sarCensus()`): the test enumerates
   `RSOperatorRegistry::instance().operatorNames()` and fails on an
   unclassified operator or a stale table row. Eight operators are the
   first-class radiometric family (`calibrate`, `backscatter`, `speckle`,
   `terrain_flatten`, `terrain_correction`, `geocode`, `ratio`, `texture`); the
   remaining fifteen are exempt with the reason recorded in the table
   (complex/phase products, geometry/displacement fields, polarimetric and
   dual-pol feature cubes, masks, temporal aggregates).
2. **Derived products.** `rs:sar_ratio` writes `SICNU_RADIOMETRIC_STATE` and
   `SICNU_SAR_CALIBRATION` = `sar_pair_metric`; `rs:sar_texture` writes
   `sar_texture`. These tokens are *not* calibration states:
   `normalizeCalibration()` maps them to `""`, so `rs:sar_calibrate` and
   `rs:sar_backscatter` refuse them with a derived-product message (a pair
   metric or GLCM measure is never a backscatter calibration), and
   `rs:sar_speckle` propagates them so filtering a derived product does not
   silently drop its state.
3. **Terrain family input contract.** `rs:sar_terrain_flatten` and
   `rs:sar_terrain_correction` apply `sigma0·cosθ0/cosθi`, which is only
   lawful for sigma0 input. A declared `gamma0`/`beta0`/`dn`, a derived token,
   a conflicting declaration (`SICNU_SAR_CALIBRATION` vs
   `SICNU_RADIOMETRIC_STATE` disagreeing) or an unrecognized token is a typed
   refusal before any output is created. A legacy scene that declares nothing
   is accepted with a logged warning under the documented sigma0 assumption.
   Layover/shadow masks do not change the radiometric state: they mark
   validity per pixel, the surviving pixels stay gamma0.
4. **Geocode state block.** `rs:sar_geocode` requires the same declared sigma0
   contract (its gamma0 band applies `sin(θL)/sin(θ0)`, Ulander 1996 / Small
   2011 eq. 5) and, like the terrain family, refuses a declared
   `SICNU_SAR_DOMAIN=db` scene (the products are linear power). The output now
   writes the full state block — `SICNU_MODALITY`,
   `SICNU_SAR_CALIBRATION=sigma0`, `SICNU_SAR_DOMAIN=linear_power`,
   `SICNU_RADIOMETRIC_STATE=sigma0` — plus the additive per-band map
   `SICNU_SAR_GEOCODE_BAND_STATES = sigma0,gamma0,incidence_deg,
   local_incidence_deg,mask_class` (the product is inherently mixed; one dataset
   token cannot describe five bands). `result.bandStates` mirrors the key.
   On the legacy undeclared path the sigma0 assumption is persisted as
   `SICNU_SAR_STATE_ASSUMED=sigma0_legacy_undeclared` (same for the terrain
   operators), so downstream guards can see the assumption instead of trusting
   a bare token. **gamma0 vocabulary**: within this family the `gamma0` token
   means *terrain-flattened gamma0* (RTC factor applied). `rs:sar_backscatter`'s
   gamma0 is a different product — a pure geometric normalization
   (sigma0/cos θ) — and must be converted back with `gamma0ToSigma0` before
   entering this family; the geocode refusal covers every gamma0 flavor.
5. **DN LUT calibration.** `rs:sar_calibrate` accepts a per-row calibration
   LUT: a plain-text sidecar with exactly one finite, positive calibration
   constant per input row, referenced by the `calibrationLut` parameter or the
   declared `SICNU_SAR_CALIBRATION_LUT` metadata (resolved relative to the
   raster and confined to its directory — a declared path that escapes it is
   refused). The explicit `calibrationLut` parameter overrides the declared
   sidecar and is used verbatim (CWD-relative or absolute). Per pixel/row r: `sigma0 = (DN² − noiseLinear)/A(r)²`, with the
   same NoData and nonpositive-power policies as the constant path.
   Interpolation is defined as *none* — a row-count mismatch (in either
   direction), an unreadable file, a non-numeric or non-positive entry, a
   mid-stream I/O error, or a sidecar beyond the per-row byte budget is a
   typed refusal; the constant A is never substituted for an unreadable
   calibration contract. Containment compares canonical paths where they
   resolve (symlinks included) with a lexical fallback; a raster at the
   filesystem root has no containing subtree and is refused.
   Annotation-XML LUTs (Sentinel-1 style) are not parsed and are documented as
   unsupported; the GF-3 adapter likewise invents no constants (ADR 0159).
6. **E2E provenance.** `tests/test_sar_radiometric_state.cpp` runs
   `import (DN) → calibrate → speckle → terrain_flatten (→ gamma0) → ratio`
   plus a `geocode` branch, asserts the declared state after every step,
   refuses double calibration, geocoding of a gamma0 product, a ratio of
   derived products, mixed-state ratio inputs, and missing/malformed LUTs
   (each with no partial output), and reopens every chain output to confirm
   the metadata survives.
7. Evidence: `tests/test_sar_radiometric_state.cpp` (census completeness,
   behavioral state gate, terrain refusals + legacy warning, geocode state
   block + conflict refusal, derived-token refusals, LUT hand-computed pixel
   oracle + refusal matrix, E2E chain).
