# PLAN — cn-eo-products-sensor-physics-10

## Architecture (target)

```
data/products/sensor_profiles/<family>.json     ← sensor truth authority (versioned)
src/geospatial/products/sensor_profile.{h,cpp}  ← loader + typed access (Qt-free)
src/geospatial/products/cn_product_metadata.cpp ← generation-aware dispatch, diagnostics
src/geospatial/products/cn_product_adapters.cpp ← + new family adapters
src/operators/rs/rs_product_import_plan.{h,cpp} ← ImportPlan service (identify→…→provenance)
src/operators/rs/rs_<family>_import_operator.*  ← thin family wrappers (schema+meta only)
data/processing/algorithm_meta/capability/*.json← agent capability per operator
```

### ImportPlan pipeline (§F contract)

identify → inspect (sidecar generation + declared fields) → validate (completeness
verdict) → resolve constituents (sidecar/image/RPC/PAN-MS pairing) → role map (registry)
→ optional calibration (declared coefficients applied or refused with reason) →
stack/virtualize (`stackToGeoTiff`, unchanged) → provenance (identity, sensor profile id
+ version, generation, declared flags, missing fields, numeric-domain state, calibration
application record).

### Registry schema (v1, per family file)

```json
{ "version": 1, "source": "<provenance>", "sensors": {
  "<sensorKey>": {
    "satellite": "GF4", "instrument": "PMS", "sensor_mode": "PMS",
    "gsd_m": 50.0, "modality": "optical", "polarizations": [],
    "bands": [ { "band": "B1", "role": "blue", "role_reason": "",
                 "wavelength_nm": 470.0,          // range midpoint (documented)
                 "center_wavelength_nm": 485.0,   // nominal center (documented)
                 "fwhm_nm": 70.0,                 // only when documented
                 "spectral_range_um": "0.42-0.52", "gsd_m": 50.0, "note": "…" } ],
    "constituents": ["sidecar_xml","image_tiff","rpc_rpb"],
    "calibration_rule": "radiance = DN * gain + bias (declared GainVal/OffsetVal)",
    "qa_vocabulary": [], "notes": "…" } } }
```

Rules: loader fail-closed; `unknown` role requires `role_reason`; forward compat =
unknown keys ignored + reported; center/midpoint distinction mandatory in `note`.

### Generation model (§D)

- `cresda_legacy_metainfo` (GF-1/2 era `<MetaInfo>`)
- `cresda_current_metadata` (`<ProductMetaData>`, CenterTime/ImageGSD/SolarZenith)
- `cbers4_inpe` (CBERS-04 XML, INPE tags)
- detection = root element + discriminator tags; result carries generation id + version +
  bounded unknown-element diagnostics (count + first N unmatched names); unknown
  generation on a recognized family → `UnsupportedVersion` verdict, never garbage.

## Phases

| Phase | Content | Budget (M) |
| --- | --- | ---: |
| 0 | baseline/audit/planning (this dir) | 18 |
| 1 | WP-A1: sensor profile loader + registry data for existing sensors; band_roles reads registry | 45 |
| 2 | WP-A2: generation detection + diagnostics + typed ambiguous-path refusals; RPC/.rpb declared capture | 48 |
| 3 | WP-B: new families (HJ-2 CCD, GF-4, GF-7, ZY-1 02C/02D, CBERS-04 MUX/PAN/WFI, GF-3 declared-metadata) | 55 |
| 4 | WP-C: ImportPlan service + optional calibration + operator rebase | 45 |
| 5 | WP-D: capability metadata + docs + GUI/agent integration checks | 30 |
| 6 | WP-E: adversarial/edge/scale tests (encoding paths, big dirs, corrupt XML, duplicate scenes) | 24 |
| 7 | independent review (2 read-only subagents) | 20 |
| 8 | review fixes + re-verify | 10 |
| 9 | final verification + rebase + PR | 5 |
| **Σ** | | **300** |

## Milestones

M1 registry authority live (existing sensors green) · M2 generations+diagnostics ·
M3 new families import headless · M4 ImportPlan + calibration · M5 capability/docs ·
M6 review clean (P0/P1=0) · M7 PR open.

## DECISIONS (running log — see DECISIONS.md)

D-01 registry replaces compiled tables for CN sensors; band_roles kept loading for back-compat.
D-02 new families reuse the CRESDA scanner where formats match; CBERS gets a third generation parser.
D-03 GF-3 = declared-metadata support (identity+polarization+mode+geometry declared fields),
     import via single-band stack with typed SAR notes; no speckle/geometry processing here.
D-04 optional calibration applies radiance = DN*gain+bias only when every requested band has
     declared gain+bias; otherwise typed refusal listing missing coefficients.
D-05 midpoint vs center: registry carries both fields; consumers pick explicitly.
