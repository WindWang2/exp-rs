# Sensor profile registry schema — v1 / v2

> Authority: `data/products/sensor_profiles/*.json` · Loader/validator:
> `src/geospatial/products/sensor_profile.{h,cpp}` · ADR 0147 (v1), ADR 0159 (v2)

This registry is the **single sensor-truth authority** for the CN satellite
families: one JSON file per family (`gaofen.json`, `zy3.json`, `zy1.json`,
`hj.json`, `cbers.json`), one entry per sensor key, one `bands` array per
entry. The loader is fail-closed; this document is the field contract the
loader enforces.

## File level

| Field | Type | v1 | v2 | Notes |
|---|---|---|---|---|
| `version` | int | required, must be supported | required, must be supported | supported = {1, 2}; anything else is a typed refusal, never a best-effort parse |
| `source` | string | optional | **required non-empty** | provenance note; names the published specification the values come from |
| `sensors` | object | required non-empty | required non-empty | key = sensor key (stable id consumed by code) |

Sensor keys are unique across the whole registry — redeclaring a key in a
second file is a validator finding.

## Sensor entry level

| Field | Type | Unit | v1 | v2 | Notes |
|---|---|---|---|---|---|
| `satellite` | string | — | optional | **required non-empty** | "GF1", "ZY3", "CBERS04", … |
| `instrument` | string | — | optional | **required non-empty** | "PMS", "WFV", "AHSI", … |
| `sensor_mode` | string | — | optional | **required non-empty** | camera/mode token ("PMS", "FWD-PAN", …) |
| `modality` | string | — | optional (default "optical") | required from {optical, sar, thermal, hyperspectral} | v2 is a closed vocabulary |
| `gsd_m` | number | metres | optional | optional, **finite > 0** when present | nominal GSD |
| `pan_variant` | string | — | optional | optional, non-self | sensor key of the panchromatic sibling; must resolve to a declared key (validator) |
| `ms_variant` | string | — | optional | optional, non-self | multispectral sibling; same rule |
| `constituents` | string[] | — | optional | optional, values non-empty | expected product constituents (`sidecar_xml`, `image_tiff`, `rpc_rpb`); unknown values are validator findings, not load errors |
| `calibration_rule` | string | — | optional | **required non-empty** | declared-coefficient semantics, verbatim text |
| `qa_vocabulary` | string[] | — | optional | optional | documented QA flag names |
| `bands` | object[] | — | **required non-empty** | **required non-empty** | per-band truth, fully written out |
| `band_axis` | object | — | — | optional | hyperspectral axis description (see below) |

Any key outside this contract is **ignored but reported** on the loaded
record (`unknownKeys`) — forward compatibility is explicit, never silent,
and shows up as a validator finding for in-repo files.

## Band level

| Field | Type | Unit | v1 | v2 | Notes |
|---|---|---|---|---|---|
| `band` | string | — | required non-empty | required non-empty, **unique per entry (case-insensitive)** | native band id ("B1", "MS1", "B001") |
| `role` | string | — | "unknown" requires `role_reason` | **must be in the ADR 0065 vocabulary**: coastal, blue, green, red, red_edge, nir, narrow_nir, swir1, swir2, cirrus, panchromatic, thermal, qa, scene_classification, unknown | mirrors `src/data/band_role.h` |
| `role_reason` | string | — | required when role = unknown | same | why no canonical role exists |
| `wavelength_nm` | number | nm | optional | optional, **finite > 0**; must agree with `spectral_range_um` (see below) | documented midpoint, or the published centre when `center_wavelength_nm` is present |
| `center_wavelength_nm` | number | nm | optional | optional, **finite > 0**; must lie inside `spectral_range_um` | published nominal band centre only |
| `fwhm_nm` | number | nm | optional | optional, **finite > 0**; must not exceed the declared range width | published FWHM only — **a range width is never a FWHM** |
| `spectral_range_um` | string | µm | optional (verbatim) | optional; if present must parse as `"lo-hi"` with hi > lo | "0.45-0.52" |
| `gsd_m` | number | metres | optional | optional, **finite > 0** | band-level nominal GSD |
| `note` | string | — | optional | optional | free text |

### Wavelength agreement rule (v2)

When `spectral_range_um` parses, one of the following must hold:

- `center_wavelength_nm` present → `wavelength_nm == center_wavelength_nm`
  (±0.5 nm rounding slack) and the centre lies inside [lo, hi];
- otherwise → `wavelength_nm` equals the range midpoint
  `(lo + hi)/2` (±0.5 nm rounding slack).

Midpoint and published centre are different documented quantities; both are
carried explicitly and must never contradict the verbatim range they came
from.

## `band_axis` (v2, hyperspectral axes)

```json
"band_axis": {
  "count": 330,
  "ordering": "B1..B76 VNIR ascending wavelength, B77..B330 SWIR ascending wavelength",
  "bad_bands": ["B123", "B124"]
}
```

| Field | Type | Notes |
|---|---|---|
| `count` | int > 0 | must equal `bands` array length |
| `ordering` | string | declared ordering note, verbatim passthrough |
| `bad_bands` | string[] | band ids flagged dead/unusable; every id must exist in `bands` |

The `bands` array stays the per-band truth and is written out in full —
`band_axis` never generates bands at runtime; it only describes extent,
ordering and bad-band flags for consumers.

## Validator & drift gate

`validateSensorProfiles()` (loader header) validates every registry file
under its declared version's rules plus the registry-wide cross-references
(dangling `pan_variant`/`ms_variant`, duplicate sensor keys, v2 `source`
presence) and returns a finding list — it reports, it never throws. The
registry drift test (`tests/test_sensor_schema.cpp`,
"the committed registry passes the validator") pins the committed data to
**zero findings**: any hand-edit that violates this contract fails CI-independent
local drift review before it can ship.
