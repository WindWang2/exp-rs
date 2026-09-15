# ADR 0159: CN Sensor Registry Schema 2.0 & Extended Product Families (GF-3/4/5, ZY-1 02B/02D/02E, CBERS)

## Context

ADR 0147 gave the platform a data-driven sensor registry
(`data/products/sensor_profiles/*.json`) and ADR 0157 the CRESDA import
adapters. The 10.0 track closed with two explicit debts: the registry had
**no per-field type/unit guards** (correctness leaned on hand-written tests
alone), and the next wave of CN families — **GF-3 (SAR), GF-4 (GEO),
GF-5/AHSI (hyperspectral), ZY-1 02B/02D/02E, CBERS (INPE distribution)** —
remained *recognized but refused*, because their sidecar layouts were not
modelled and the contract refuses to guess.

Meanwhile real teaching packages exist for these families, and PR #1008
(D13) landed a general radiometric FSM at a different layer
(`src/core/radiometric_state`), which makes *declared-coefficient
provenance* at the product layer the complementary — not duplicative —
contribution.

## Decisions

1. **Schema 2.0 is additive; v1 stays readable.** The version gate accepts
   {1, 2}. v1 files keep the historical rules; v2 files get strict
   per-field validation: required non-empty identity fields, closed
   `modality` vocabulary, finite-and-positive physical quantities
   (`gsd_m`, `wavelength_nm`, `center_wavelength_nm`, `fwhm_nm`), closed
   ADR 0065 role vocabulary, case-insensitive band-id uniqueness, and a
   wavelength-agreement rule that pins `wavelength_nm` to the declared
   spectral range (published centre when present, otherwise the range
   midpoint ±0.5 nm rounding slack). Field contract: `docs/products/SENSOR_SCHEMA.md`.

2. **Validator + drift gate, not just loader throws.**
   `validateSensorProfiles()` walks every registry file, validates each
   entry under its declared version's rules (reporting findings instead of
   throwing, so one bad entry never hides the others) and checks the
   registry-wide cross-references (dangling `pan_variant`/`ms_variant`,
   duplicate sensor keys, v2 `source` presence). The committed registry is
   pinned to **zero findings** by `tests/test_sensor_schema.cpp` — a
   hand-edit that violates the contract fails local drift review.

3. **Hyperspectral band axes are written out, never generated.** GF-5/ZY-1
   02D/02E-class layouts land as a fully expanded `bands` array (per-band
   truth on disk, auditable); an optional v2 `band_axis` block describes
   extent (`count`, must equal the array length), ordering (verbatim note)
   and `bad_bands` (ids that must exist). No runtime band-generation
   formula exists anywhere in the loader.

4. **New families at declared-metadata level, reusing the one seam.**
   GF-3, GF-4, GF-5, ZY-1 02B/02D/02E and CBERS enter the existing
   identify → parse → registry → import-plan pipeline:
   - GF-3 SAR: identity/mode/polarization/orbit/level parsed from its
     sidecar; numeric domain stays `digital_number` with an explicit SAR
     note; no σ⁰ kernel is claimed (SAR physics stays out of the product
     layer).
   - GF-4: geostationary semantics carried as declared passthrough
     (sub-satellite point when the sidecar declares one).
   - GF-5/ZY-1 02D/02E AHSI: hyperspectral registry entries + GDAL
     subdataset inventory; a missing HDF5 driver at runtime is a typed
     refusal, not a silent unknown.
   - CBERS: an explicit third sidecar generation (`cbers_inpe_*`) with its
     own parser function; CRESDA and INPE schemas never mix. Unknown
     variants stay refused with a concrete reason.
   Unknown generations inside every family keep the
   `UnsupportedVersion` + bounded-diagnostics contract of ADR 0147.

5. **Physical quantities carry source, unit and provenance or stay absent.**
   Registry values must name their published source in the file-level
   `source` note (v2 requirement); fields a sidecar does not declare are
   reported as missing, never defaulted (ADR 0157 policy unchanged).
   Declared gain/bias coefficients are transported verbatim by the product
   layer; unit conversion physics belongs to the radiometric layer (ADR 0158),
   which the product layer does not duplicate.

6. **ADR numbering skips 0158** (claimed by the in-flight radiometric FSM
   PR #1008) to avoid a collision; this record is 0159.

## Consequences

- The registry is now self-policing: schema violations surface as precise
  named findings from the validator and as typed load errors at runtime,
  not as silent field drops.
- Adding family #N is a data change plus identity pattern plus (when the
  sidecar layout differs) a parser function — the import-plan seam and the
  GUI/CLI/agent surfaces stay unchanged.
- Tests assert against golden metadata hand-derived from published
  specifications, independent of the parser under test.
