# Radiometric Physics 11 — Solar Geometry, Transition Authority, Provider Seam, BRDF & QA Flags

> Domain doc for the radiometric-physics-11 track. Companions:
> [grid-and-radiometric-policy.md](grid-and-radiometric-policy.md) (state/scale
> policy, ADR 0114), [foundation-5.md](foundation-5.md),
> [validation-policy.md](validation-policy.md). New modules live in
> `src/processing/algorithms/{solar_geometry,radiometric_transition,atmospheric_provider,
> brdf_normalization,radiometric_qa}.{h,cpp}` with tests
> `tests/test_{solar_geometry,radiometric_transition,atmospheric_provider,
> brdf_normalization,radiometric_qa}.cpp`.

## 1. Where this sits in the optical chain

```
DN ──(RadiometricCalibration + SolarGeometry)──▶ radiance / TOA reflectance
   ──(AtmosphericProvider: dos1 | dos2 | quac | future LUT)──▶ surface reflectance
   ──(BrdfNormalization: Ross-Li kernels)──▶ view-angle-normalized reflectance
   ──(TopographicCorrection, Foundation 5.0)──▶ terrain-illumination-corrected
RadiometricQa flags propagate alongside every step; RadiometricTransition
plans each conversion edge and emits the provenance record.
```

Unit states follow ADR 0114 exactly (`SICNU_RADIOMETRIC_STATE`: `digital_number`,
`radiance`, `toa_reflectance`, `surface_reflectance`, `brightness_temperature`).
BRDF, topographic and QA steps preserve the unit state (they are multiplicative
corrections or flag-only outputs).

## 2. Solar-earth geometry (`SolarGeometry`)

Spencer (1971) Fourier series + the NOAA general solar-position form, all in
double, no atmospheric refraction (calibration uses the true geometric
position):

- Fractional year `Γ = 2π(n − 1 + (h − 12)/24)/365` (n = day of year, h = UTC
  decimal hour; leap years use the standard 365-day series — sub-0.01° effect).
- Declination `δ` (radians): the seven-term Spencer series (≤ 0.0006 rad error).
- Equation of time (minutes): the five-term Spencer series (≤ ~1 s error).
- True solar time `tst = 60·h + EoT + 4·longitude` (UTC input ⇒ timezone term 0);
  hour angle `ha = tst/4 − 180`.
- `cos z = sinφ·sinδ + cosφ·cosδ·cos ha`; azimuth from north clockwise via
  `atan2(−cosδ·sin ha, sinδ·cosφ − cosδ·sinφ·cos ha)` — the same horizontal
  frame as the topographic-correction illumination cosine.
- Earth-sun inverse-square factor `E₀ = d⁻²`: the Spencer radius-vector series
  (perihelion ≈ Jan 3 → 1.0351, aphelion ≈ Jul 4 → 0.9661); `d = 1/√E₀` in AU.
  `E₀` is the multiplier in the ESUN TOA form `ρ = π·L·E₀/(ESUN·cos θz)`.

Fail-closed: invalid date/time, latitude outside [−90, 90], longitude outside
[−180, 180], or any non-finite input refuses with a typed message; `acos`/
`asin` arguments are clamped so no NaN can be produced silently.

Operator: `rs:solar_geometry` (date + UTC time + scene centre; optional
in-place metadata stamp: `SICNU_SUN_ZENITH`, `SICNU_SUN_AZIMUTH`,
`SICNU_SUN_ELEVATION`, `SICNU_EARTH_SUN_FACTOR`, `SICNU_EARTH_SUN_DISTANCE_AU`).
Acquisition instants come from scene metadata the caller already has (Landsat
MTL `DATE_ACQUIRED` + `SCENE_CENTER_TIME`; Sentinel-2 MTD `Sensing_Time`).

## 3. Transition authority (`RadiometricTransition`)

`RadiometricTransition::plan(from, to, inputs)` plans the shortest lawful
chain for one band over the ADR 0114 vocabulary and binds every edge to its
required inputs:

| Edge | Required inputs (stable missing-tokens) |
|---|---|
| `dn_to_radiance` | `hasRadiance` MTL coefficients (`radiance_coefficients`) |
| `dn_to_toa_reflectance` | Landsat: `hasReflectance` + real sun elevation (`reflectance_coefficients`, `sun_elevation`); S2/generic: loaded quantification scale/offset (`reflectance_coefficients`) |
| `radiance_to_toa_reflectance` | ESUN > 0 (`esun`) + usable sun elevation (`sun_elevation`) — the `ρ = π·L·E₀/(ESUN·cos θz)` path |
| `radiance_to_brightness_temperature` | `K1 > 0`, `K2 > 0` (`thermal_constants`) |
| `toa_to_surface_reflectance` | a named atmospheric provider (`atmospheric_provider`) |

Lawful DAG: `DN → radiance → TOA → surface`, plus the thermal branch
(`radiance → BT`). Identity is lawful; every inversion and unit-jumping
shortcut (DN→SR, DN→BT, L→SR, TOA→BT, SR/BT → anything) is **unlawful** —
removed information cannot be conjured back and reflectance cannot become
temperature. Unknown states are refused against the closed vocabulary.

Refusals carry stable tokens (`radiance_coefficients`, `sun_elevation`, `esun`,
`thermal_constants`, `atmospheric_provider`) plus a human explanation. A
lawful-but-unsatisfiable plan still emits the provenance skeleton so audits
see what *would* be applied.

Provenance: `exp_rs_radiometric_provenance/1` JSON — `from`/`to`, the planned
`steps` with formula tokens and the coefficients/geometry actually used
(sun elevation annotated `metadata` vs `computed`), and
`numeric_scale_before`/`numeric_scale_after`. Operators write this into their
results and dataset metadata; `SICNU_NUMERIC_SCALE` is consumed by the first
edge (quantification form) and outputs carry scale 1.

Scaled DN stacks (Sentinel-2 L2A `SICNU_NUMERIC_SCALE` = 10000) stay an
import-time concern (grid-and-radiometric policy §2); there is deliberately no
`DN → surface_reflectance` unboxing edge — an import-stamped L2A stack is
already surface reflectance with a scale, not a conversion target.

## 4. Atmospheric provider seam (`AtmosphericProvider`)

The built-in image-space methods stay authoritative in `AtmosphericCorrection`;
the seam makes them selectable *by id* alongside future external providers:

- `SurfaceReflectanceProvider` interface: `id()`, `displayName()`,
  `requirements()` and per-band (`toSurfaceReflectance`) and/or multi-band
  (`toSurfaceReflectanceMultiBand`, for statistics methods) kernels returning
  typed errors.
- In-process registry (mutex-guarded, idempotent built-ins, providers are
  process-lifetime stable): built-ins `dos1` (Chavez TOA-space dark-object
  subtraction), `dos2` (+ transmittance) and `quac` (multi-band statistics
  form over reflectance stacks).
- `resolve(id, aux)`: an **unregistered id is a typed refusal** naming the
  requested id and the registered alternatives — never a silent DOS fallback;
  a registered provider with missing/degenerate auxiliary inputs (sun
  geometry, scene dark level, transmittance ∉ (0,1], AOD, water vapour, target
  elevation) is refused with every missing item named.

A future 6S-class LUT provider registers itself and declares
`needsAod/needsWaterVapour/needsTargetElevation`; no platform change is
required. (An in-tree 6S LUT implementation is out of scope here and owned by
the spectral-workbench effort.)

## 5. BRDF / view-angle normalization (`BrdfNormalization`)

Kernel pair (Lucht, Schaaf & Strahler 2000; MODIS BRDF/Albedo ATBD):

- Ross-Thick volumetric: `k_vol = ((π/2 − ξ)cos ξ + sin ξ)/(cos θs + cos θv) − π/4`
  with `cos ξ = cos θs cos θv + sin θs sin θv cos Δφ`. Special value
  `k_vol(0,0,·) = 0`.
- Li-Sparse-Reciprocal geometric with `(h/b) = 2`: overlap integral
  `O = (t − sin t cos t)(sec θs + sec θv)/π`,
  `cos t = 2√(D² + tan²θs tan²θv sin²Δφ)/(sec θs + sec θv)`,
  `D² = tan²θs + tan²θv − 2 tan θs tan θv cos Δφ`,
  `k_geo = O − sec θs − sec θv + (D + D′)/2`,
  `D′ = √(D² + 4 tan²θs tan²θv sin²Δφ)`. Special value `k_geo(0,0,·) = −1`.
- Anisotropy factor `f = 1 + f_vol·k_vol + f_geo·k_geo > 0` (nonphysical
  factor ⇒ typed refusal); normalization is `ρ_ref = ρ_obs·f(G_ref)/f(G_obs)`
  with reference geometry nadir-view/unchanged-sun by default.

Required angle metadata (typed refusal when absent): sun zenith/azimuth
(parameters or `SICNU_SUN_*`), view zenith/azimuth (parameters or
`SICNU_VIEW_*`). Kernel weights `f_vol`/`f_geo` are band-specific physical
quantities — they must come from multi-angle fits or published per-biome
values; a single scene cannot estimate them, and this module refuses to guess.
For angle-less two-date comparisons the `PairRegression` c-factor API
(OLS `y = a + b·x`, `c = a/b`, `ρ₂' = c·ρ₂`, Schott-style leveling) levels one
date onto another with explicit validity conditions (minimum pairs, non-
degenerate slope, positive domain).

Operator: `rs:brdf_normalization` (kernel-driven, single-pass streaming,
bit-exact grade). The empirical pair path is a module API for now; a streaming
`rs:*` wrapper is a follow-up (see PR body).

## 6. Radiometric QA flags (`RadiometricQa`)

Frozen uint16 vocabulary (values pinned by `test_radiometric_qa`):

| Bit | Flag | Meaning |
|---|---|---|
| 0 | `Saturated` | value ≥ caller saturation level |
| 1 | `Negative` | reflectance < 0 after correction |
| 2 | `OverRange` | reflectance > 1 |
| 3 | `NotFinite` | NaN / ±Inf |
| 4 | `Cloud` | propagated from a QA mask |
| 5 | `CloudShadow` | propagated |
| 6 | `Snow` | propagated |
| 7 | `SaturationQa` | QA_RADSAT-style sensor saturation bit |
| 8 | `InvalidAngles` | geometry unusable for the pixel's correction |

Semantics: flags only ever OR in (`propagate` unions; nothing downstream
clears an anomaly), `evaluateReflectance` classifies one band in one pass and
accumulates a `Summary` (per-flag counts + flagged fraction with an explicit
denominator), `markFromMask` adopts the QaMask binary convention, and
`addSaturationBits` implements the Landsat QA_RADSAT per-band-bit convention.
`propagateLinearUncertainty` carries `σ` through one linear calibration step
`y = gain·x + bias`: `σy² = (gain·σx)² + (x·σgain)² + σbias²`.

Operator: `rs:radiometric_qa` — one uint16 flag band per input band
(`0 = clean`), optional same-grid cloud mask (grid mismatch refused, never
resampled), optional in-stack QA_RADSAT band with per-band bit masks, output
metadata `SICNU_QA_FLAG_SCHEMA = exp_rs_radiometric_qa_flags/1`, and a
per-band flagged-fraction summary in the result JSON.

## 7. Determinism & resources

All new kernels are pure buffer functions evaluated in a single-threaded,
fixed order (ADR 0124 bit-exact grade). Operators stream 256×256 tiles:
`rs:brdf_normalization` ≈ 2 float tiles + accumulators, `rs:radiometric_qa` ≈
float + uint16 + optional mask tile per tile; `rs:solar_geometry` uses no
pixel buffers. No wall-clock gates anywhere; correctness rests on the
closed-form oracles in the five test executables.

## 8. Test oracles (independence)

Each test executable re-derives its expected values independently of the
production code: published seasonal extrema (declination ±23.44°, EoT
extrema +16.4/−14.2 min, perihelion/aphelion distances), Cooper's cross-formula
declination, exact trigonometric identities (noon elevation `90 − |φ − δ|`,
mirror symmetry), hand-computed kernel values, the linear-pair c-factor
identity, hand-worked Chavez/uncertainty algebra, and structural provenance
checks. Negative tests pin every typed refusal.
