# Sensor/Product Physics Platform 10.0 — Chinese EO Products (cn-eo-products-sensor-physics-10)

## Baseline

- Branch `zcode/cn-eo-products-sensor-physics-10`, worktree off `origin/master` @ `7d78059d1a`
  (PR #958 merge). ADR 0146 (#956) shipped GF-1/2/6, ZY-3, HJ-1A/1B CCD adapters; this track
  turns that fixed set into a platform (ADR 0147, `docs/adr/0147-sensor-product-physics-platform.md`).

## Architecture

1. **Sensor profile registry** — `data/products/sensor_profiles/{gaofen,zy3,zy1,hj}.json`
   (schema-versioned) is the single sensor-truth authority: platform/instrument/mode,
   modality, nominal GSD, `pan_variant`/`ms_variant` links, expected constituents, declared
   calibration rule, QA vocabulary, and bands with canonical role (+required `role_reason`
   for unknown), documented range midpoint, nominal centre and FWHM *only where published*.
   Loader `src/geospatial/products/sensor_profile.{h,cpp}`: Qt-free, fail-closed,
   version-gated, path-keyed cache, unknown-key forward-compat reporting.
   The `band_roles/*.json` tables are removed; `cnBandRoleTable` survives as a registry
   projection. The spectral resampling store (`data/spectral/sensors.json`) stays the SRF
   authority; midpoint vs centre are distinct documented quantities.
2. **Sidecar generations** — every CRESDA parse reports `parseDiagnostics{generation,
   root_element, sidecar, unknown_top_level_elements[]}`; generations:
   `cresda_legacy_metainfo` / `cresda_current_metadata` / `cresda_unknown_root`.
   Unknown roots keep the whitelisted parse and name what was not understood.
3. **Standardized ImportPlan** — `rs_product_import_plan.{h,cpp}`: identify → inspect →
   validate → resolve constituents (sidecar, image, RPC `.rpb`, PMS sibling with declared
   role) → role map (registry) → optional calibration → stack (unchanged `stackToGeoTiff`
   contract) → stamp → provenance. New operator `rs:cn_product_import`; the three family
   operators become thin adapters; result keys stay legacy-compatible (ADR 0146) with
   provenance added alongside.
4. **Optional declared calibration** — `apply_calibration=true` applies
   `radiance = DN × gain + bias` to every requested band and stamps
   `SICNU_RADIOMETRIC_STATE=radiance`; any requested band missing gain+bias refuses the
   import (typed, naming the bands). Failures remove partial output; cancellation checked.

## Major deliverables

- New supported families: **GF-7 FWD/BWD** (pan+MS per camera), **ZY-1 02C PMS/HRC**
  (new `ProductKind::Zy1Product`), **HJ-2A/B CCD**; ZY-3 FWD/BWD get their own registry
  entries. Pan/MS variant resolution is registry-driven (`pan_variant` + declared shape).
- GF-7 catch-all refusal for non-FWD/BWD GF-7 products (recognized, diagnosable).
- `discoverCn` in `src/processing/algorithms/satellite_products.cpp` (ProductType::Cn):
  the GUI product-import dialog and `CollectionImportService` now discover CN products
  through the same registry-backed authority; new menu entry「导入国产卫星产品...」.
- RPC (`.rpb`) located as a declared constituent; completeness verdicts
  (`complete`/`partial_readable`) with `missingConstituents[]`.
- Capability knowledge: 4 new sidecars (repairs inherited #956 gap), enrichment entries,
  family map, pins 111→115; help descriptors + generated reference pages updated.

## Compatibility

- ADR 0146 operator result keys preserved verbatim (`productKind`, `bandSource`,
  `bands[]`, `bandRoles[]`, `declared{}`, `missingDeclaredFields[]`, ...); new provenance
  keys are additive (`sensorProfile`, `sidecarGeneration`, `completeness`, `calibration{}`,
  `siblingImage/Role`, `bandWavelengthsNm[]`, ...).
- `cnSensorKey` behavior for existing families preserved (verified per key); registry
  unavailable → identity key, callers still fail closed.
- ZY3_FWD/BWD identity now resolves to dedicated `zy3_fwd`/`zy3_bwd` registry entries
  (same physical band facts as the former `zy3_pan` fallback).

## Tests

- `tests/test_cn_products.cpp`: registry authority/schema/forward-compat, generation
  detection + unknown-element diagnostics, RPC + PMS-sibling constituents, GF-7 / ZY-1 02C /
  HJ-2 imports (unified + family operators), apply_calibration DN→radiance numeric check +
  typed partial-coverage refusal, corrupt/non-CRESDA/ambiguous/duplicate scenes, Chinese
  paths, 520-file inflated directories, read-only source dirs, GUI discovery routing.
- Determinism: imports are bit-exact copies/affine transforms of source pixels.

## Performance / resource

- No new unbounded scans: directory enumeration stays bounded (512 entries); calibration
  applies a chunked (4096 px) in-place affine transform; memory profile unchanged
  (one band buffer + fixed overhead).

## Review findings

- Independent read-only review (architecture + science correctness) found 1 P0 (stacking
  source-band semantics), 3 P1 (diagnostics depth, test compile, missing capability
  sidecars), 3 P2 (half-calibrated output on IO failure, test cache isolation, GF-7 B1
  midpoint), 9 P3 — **all P0/P1/P2 fixed**; accepted debt recorded in
  `.planning/cn-eo-products-sensor-physics-10/REVIEW_LOG.md` (registry type hardening,
  double directory scan, midpoint-vs-centre consumer semantics).

## Known limitations / follow-ups

- GF-3 SAR, GF-4, GF-5/AHSI-class, ZY-1 02B/02D/02E, CBERS remain *recognized but not
  adapted* — their sidecar layouts are not documented in-repo and the contract refuses to
  guess. CBERS files produced outside CRESDA (INPE format) will parse as unknown
  generation with full diagnostics.
- Registry schema type-hardening (per-field guards) deferred; current guarantee relies on
  the in-repo registry + tests.
- `docs/generated/help` and `pi/knowledge/capability-*.md` are generator outputs; this PR
  hand-extends them consistently with the committed data (regenerate with
  `capability_knowledge_tool gen-pages` on next refresh).

## Local evidence only; no online CI dependency

All evidence (configure/build/test exits, outputs) is recorded in
`.planning/cn-eo-products-sensor-physics-10/EVIDENCE.md`. No GitHub Actions run was
triggered, awaited, or cited.
