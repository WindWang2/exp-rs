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

## 3. Full range-Doppler: explicit refusal + extension contract

Full range-Doppler terrain correction / geocoding needs orbit state vectors
and sensor timing (zero-Doppler range equations, DEM sampling in range time,
azimuth compression timing). The generic product metadata contract does not
carry them, so **no operator may approximate it** — requests are typed
refusals. The additive extension contract for future providers (parsed by
none of the current operators):

```
SICNU_SAR_ORBIT_STATES   "t;x;y;z;vx;vy;vz|..."  (UTC seconds, metres, m/s)
SICNU_SAR_RANGE_WINDOW   range gate start/stop (s)
SICNU_SAR_PRF            pulse repetition frequency (Hz)
SICNU_SAR_RANGE_RATE     range sampling rate (samples/s)
```

When a provider declares and validates these, a zero-Doppler solver may be
added behind the same operator seam; until then the limitation stands as
documented here and in the operator metadata.
