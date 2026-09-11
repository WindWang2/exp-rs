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
