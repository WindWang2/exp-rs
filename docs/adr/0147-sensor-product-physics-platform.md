# ADR 0147: Sensor/Product Physics Platform (Registry, Generations, ImportPlan)

## Context

ADR 0146 shipped GF-1/2/6, ZY-3 and HJ-1A/1B CCD as fixed-set adapters with
data-driven band-role tables. Three structural limits remained:

1. **Sensor truth was split.** `data/products/band_roles/*.json` carried
   range-midpoint wavelengths; `data/spectral/sensors.json` carried nominal
   centres + FWHM for resampling with no cross-reference; identity patterns
   were compiled in. Nothing linked the stores, and nothing beyond
   band→role+wavelength had a home (GSD, constituents, calibration rules,
   polarization).
2. **Sidecar generations were implicit.** The legacy `<MetaInfo>` and current
   `<ProductMetaData>` tag whitelists merged silently in one scan: no
   generation id in results, no unknown-element diagnostics, so a future
   CRESDA root would parse "successfully" while hiding what was not
   understood.
3. **Ingestion was operator-shaped, not service-shaped.** The shared machinery
   lived in a header (`rs_cn_import_operator.h`); calibration could be
   reported but never applied; PAN/MS relationships and RPC documents stayed
   implicit in directory contents; GUI/CLI/Agent had to key on three
   per-family operators.

## Decisions

1. **Sensor profile registry is the single band-truth authority.**
   `data/products/sensor_profiles/{gaofen,zy3,zy1,hj}.json` (schema `version`
   1) carry, per sensor key: satellite, instrument, sensor mode, modality,
   nominal GSD, `pan_variant`/`ms_variant` links, expected constituents,
   the declared calibration rule, QA vocabulary, and bands with role
   (+required `role_reason` for `unknown`), range midpoint, nominal centre
   and FWHM *only where a published source documents them*, verbatim
   spectral range text and per-band GSD. The loader
   (`src/geospatial/products/sensor_profile.{h,cpp}`) is Qt-free, fail-closed,
   schema-version-gated (future versions are a typed error, not a guess) and
   reports unknown entry keys as forward-compatibility warnings. The
   `band_roles/` tables are **removed**; `cnBandRoleTable` survives as a
   projection of the registry. The spectral resampling store keeps ownership
   of SRFs; the registry carries its own documented centres/FWHM so the
   consumer chooses the physical quantity explicitly.

2. **Sidecar generations are explicit.** Every CRESDA parse reports
   `parseDiagnostics{generation, root_element, sidecar,
   unknown_top_level_elements[]}` on `ProductMetadata` (serialized in
   `toJson`). Generation ids: `cresda_legacy_metainfo`,
   `cresda_current_metadata`, `cresda_unknown_root`. An unknown root never
   blocks the whitelisted parse and never hides itself; unknown top-level
   elements are reported bounded (16 names). Import results surface
   `sidecarGeneration`/`sidecarRoot`/`warnings`.

3. **Support set grows along the same rules.** New supported families:
   GF-7 FWD/BWD (PAN 0.45–0.90 µm + MS B1–B4 per camera), ZY-1 02C PMS
   (B1–B4 + pan) and HRC (2.36 m pan), HJ-2A/B CCD (B1–B4, 16 m). ZY-3
   FWD/BWD get their own registry entries (3.5 m). Everything else stays a
   typed refusal with a concrete reason; GF-3/GF-4/GF-5, ZY-1 02B/02D/02E,
   CBERS and HJ IRS/HSI remain *recognized but not adapted* — product-level
   facts only, no invented layouts. Pan-vs-MS resolution becomes
   registry-driven: the profile's `pan_variant` link + the sidecar's declared
   shape (ModeID or 1-band inventory) — never band-number guessing.

4. **One standardized ImportPlan service** (`rs_product_import_plan.{h,cpp}`,
   operator `rs:cn_product_import`): identify → inspect → validate → resolve
   constituents (sidecar, image, RPC `.rpb`, PAN/MS sibling with declared
   role) → role map (registry) → optional calibration → stack → stamp →
   provenance. The per-family operators (`rs:gaofen_import`, `rs:zy3_import`,
   `rs:hj_import`) become thin adapters over the same service; their result
   keys stay stable (courses depend on them) with new provenance added
   alongside: sensor profile id/version, sidecar generation, completeness
   verdict (`complete`/`partial_readable`), `missingConstituents[]`,
   `warnings[]`, `declared.rpc`, `bandWavelengthsNm[]`,
   `calibration{}` and `siblingImage`/`siblingRole`.

5. **Declared-coefficient calibration is applicable, never partial.**
   `apply_calibration=true` converts every requested band with
   `radiance = DN × gain + bias` (CRESDA `GainVal`/`OffsetVal` semantics) and
   stamps `SICNU_RADIOMETIC_STATE=radiance`; if ANY requested band lacks
   gain+bias the import refuses with a typed error naming the bands —
   no mixed-state output, no half-calibrated stacks.

6. **Ambiguity refuses, order never decides.** Multi-image product
   directories without a stem-paired image keep the ADR 0146 refusal; the
   plan layer reports it as a typed `FileNotFound` with the pairing rule in
   the message instead of picking an arbitrary sibling.

## Consequences

- Band truth has exactly one in-repo home; the drift example (GF-PMS 470 nm
  midpoint vs 485 nm centre) is now two named fields of one record with
  documented semantics, cross-checked by tests.
- Agents can ask "what is this product, what bands, is it usable, what is
  missing" from one plan/result payload; the capability knowledge entries
  for all four import operators pin that surface.
- New CRESDA-distributed families are a registry entry + identity pattern +
  tests (and, when the format differs, a new generation id) — no parser
  forking.
- GF-3 SAR, GF-4, GF-5/6 AHSI-class hyperspectral, ZY-1 02D/02E and CBERS
  remain future work: their sidecar layouts are not documented in-repo, and
  the contract refuses to guess.
