# sicnu.asset_state.v1 — schema reference

## Document shape

```jsonc
{
  "schema": "sicnu.asset_state.v1",
  "identity":   { "asset_id", "revision", "source_path", "display_name",
                  "kind", "lifecycle", "persistence" },
  "sensor":     { "platform", "instrument", "sensor_key", "modality",
                  "product_family", "product_id", "processing_level" },
  "acquisition":{ "time_iso", "time_source", "precision", "valid" },
  "bands": [ { "index", "name", "role", "wavelength_nm", "fwhm_nm", "data_type",
               "no_data_value", "scale", "offset", "radiometric_unit", "mask_band" } ],
  "geometry":   { "has_crs", "crs_wkt", "crs_authid", "crs_geographic",
                  "crs_projected", "geo_transform": [6], "pixel_size_x",
                  "pixel_size_y", "width", "height", "min_x", "min_y",
                  "max_x", "max_y" },
  "validity":   { "no_data_policy", "cloud_cover_percent", "quality_mask_info" },
  "radiometric":{ "unit", "declared_raw", "domain", "numeric_scale" },
  "temporal":   { "present", "refs": [ { "collection_id", "role" } ], "truncated" },
  "provenance": { "is_derived", "algorithm_id", "algorithm_version",
                  "inputs": [ { "asset_id", "revision", "band_references",
                                "value_domain" } ], "completed_at_utc",
                  "execution_fingerprint", "software_version", "workflow_ref",
                  "cache_hit" },
  "model_derived": { "present", "model_kind", "labels", "accuracy",
                     "sidecar_path", "feature_schema" },
  "confidence": 0.0..1.0,
  "assumptions": [ /* sorted, unique */ ],
  "unknowns":    [ /* sorted field paths */ ],
  "claims": [ { "path", "kind", "sources", "note", "alternatives" } ],  // sorted by path
  "notes":   [ { "code", "path", "detail" } ]                           // sorted
}
```

Determinism: jsoncpp objects serialize in lexicographic key order; every
array is explicitly normalized (`normalizeState`) before writing. The same
state always serializes to the same bytes on the same build; `fromJson` is
byte-stable through round-trips.

Fidelity: `has*`/presence flags are derived from key presence on parse
(`wavelength_nm` present ⇔ `hasWavelengthNm`), so "unset" and "0" never
collapse. Absent optional keys are omitted; the claim for such a field is
`unknown` (recorded in `unknowns`), never a fabricated value.

## Vocabularies

- `identity.kind`: `unknown | raster | vector | remote_map | virtual_raster`
  (mirrors `sicnu::data::AssetKind`).
- `identity.lifecycle`: mirrors `sicnu::data::AssetState` (`registered`,
  `resolving`, `ready`, `missing`, `unavailable_source`, `offline`,
  `authentication_required`, `error`, `stale`).
- `sensor.modality`: `unknown | optical | sar | thermal | hyperspectral`.
- `radiometric.unit` (normalized, case-insensitive on input):
  `digital_number` (also `dn`), `radiance`, `toa_reflectance`,
  `surface_reflectance` (also `boa_reflectance`), `brightness_temperature`,
  and the SAR family `sigma0 | gamma0 | beta0`. `radiometric.domain`
  (`linear_power | db`) is recorded verbatim and never interpreted.
- `bands[].role`: the `sicnu::data::BandRole` lowercase vocabulary
  (`coastal`, `blue`, …, `qa`, `scene_classification`).
- `claim.kind`: `known | inferred | assumed | unknown | conflicted` —
  see overview.md for the evidence contract.

## Evidence lattice rules (resolver)

1. Declarative sources first: a file/catalog/sidecar declaration outranks
   family-level truth (sensor profiles).
2. One distinct normalized value across observations → `known` if any
   declarative source agrees, `inferred` if only inferential sources exist;
   sources lists merge.
3. Distinct declarative values → `conflicted`; alternatives sorted+unique;
   the field value stays empty. The SAR dual-key disagreement
   (`SICNU_SAR_CALIBRATION` vs `SICNU_RADIOMETRIC_STATE`) projects this
   conflict, mirroring `readDeclaredSarState` — no silent resolution.
4. No observation + documented default → `assumed` with a note
   (`radiometric.fsm_default`, SAR legacy assumption).
5. No observation + no default → `unknown` and an `unknowns` entry.

## Confidence

`confidence = Σ score(path) / |applicable paths|`, rounded to 3 decimals.
Scores: known 1.0, inferred 0.75, assumed 0.25, unknown/conflicted 0.
Applicability: `identity.asset_id` only with a catalog; `geometry.crs` only
with a dataset; `provenance.algorithm` only with a derivation record;
`validity.no_data_policy` only with bands; `bands[*].role` scores the worst
band (full credit only when every band resolves). Absent sources never
dilute the denominator. Pinned scenarios live in
`tests/test_scientific_state_diff.cpp` and `test_scientific_state_fixtures.cpp`.

## sicnu.asset_state_diff.v1

```jsonc
{ "schema": "sicnu.asset_state_diff.v1",
  "diffs": [ { "path", "kind", "before", "after" } ],  // sorted by path
  "unchanged_fields": <count> }
```

`kind`: `changed | added | removed | claim_changed` (claim records compare at
the special path `claims[<claim path>]`). Byte-deterministic, round-trips.

## Bounds

Bands 4096, claims 1024, temporal refs 256, notes 1024, metadata items 512
per scope. Overflows truncate with an explicit note (`bands.truncated`,
`temporal.truncated`) — never silently.

## Input safety

Untrusted JSON (sidecars, diff documents) parses with
`Json::CharReaderBuilder` + `stackLimit = 128` and jsoncpp exceptions caught
into typed `MalformedJson` errors. Foreign schema ids are `SchemaMismatch`
refusals.
