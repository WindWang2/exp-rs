# ADR 0156: Built-in Spectral Library and the Material-Prior Contract

## Context

Spectral matching exists as code — the spectral library domain
(`SpectralLibrary`, ADR 0081), the workbench (ADR 0092) and wavelength-aware
SAM/SID matching with `MatchScore::resampled` (ADR 0096), plus the ADR 0079
resampling kernel — but the repository shipped **no spectral data**. The
workbench opened empty, SAM/SID had nothing to match against, the D3c
hyperspectral lab chain (MNF → PPI → SAM/SID → unmixing) broke at its central
step, and the agent layers (D8 capability knowledge, D9 teaching copilot) had
no machine-queryable source of material priors ("water is dark in NIR",
"vegetation has a red edge").

Collecting measured spectra was not an option under the track's license-first
policy: the redistribution terms of the common measured libraries (ASTER /
ECOSTRESS, academic collections) are ambiguous or restrictive, and anything
with unclear terms is excluded by policy.

## Decision

Ship a **pure-data, physics-model synthetic** spectral library plus a thin
strict-validation/query layer in `SpectralLibrary` — no material knowledge is
hard-coded into C++.

1. **Data** (`data/spectral/`, tracked via a `.gitignore` exception):
   - `library.json` — format v2, 25 entries, 12 fixed material classes
     (`water`, `vegetation`, `soil`, `impervious_surface`, `bare_rock`,
     `sand`, `snow`, `ice`, `cloud`, `shadow`, `cropland`, `burned_area`);
     key classes carry ≥ 3 entries with genuine within-class variability.
     5 nm shared grid, 400–2500 nm, reflectance in [0, 1] at 4-decimal
     fixed point; ~215 KB, far under the 5 MB budget.
   - `library.schema.json` — JSON Schema of record for curated libraries.
   - `sensors.json` — nominal Landsat-8/9 OLI / Sentinel-2 MSI / GF-1/2 PMS
     band grids (public nominal facts, Gaussian SRF approximation of ADR 0079).
   - `LICENSES.md` — license + provenance roll-up; drift against the library
     is a test failure.
   - `tools/generate_library.py` — deterministic generator; the derivation of
     record; runs physics self-checks (smoke gates, taxonomy coverage).

2. **Licensing**: every entry carries `source` / `license` / `citation`;
   the shipped data is CC0-1.0 (public-domain dedication of self-authored
   synthetic curves), decoupled from application code licensing. Measured
   entries may join only with unambiguous redistribution terms and
   `synthetic: false`.

3. **Format v2, backward compatible**: v1 files (shared root grids,
   `name`+`spectrum`) keep loading through the lenient `Library::load` — the
   workbench dialog is untouched. v2 adds per-entry `id`, `subclass`,
   `license`, `citation`, `synthetic`, `derivation`, `tags`, per-entry
   grids, a `reflectance` alias for `spectrum` and a root `id`
   (`rs-studio.builtin-spectral-library.v1`) that lab experiments reference
   as `library_id`. Malformed grids are now hard errors instead of silently
   ignored (no silent dropping, ever).

4. **Strict validation is opt-in and mandatory for curated data**:
   `Library::loadValidated` applies `validateLibrary` — unique slug ids,
   strictly increasing finite wavelengths sized to the spectrum, reflectance
   finite in [0, 1], FWHM finite > 0, complete provenance, `synthetic: true`
   requires `derivation`. Every violation names the offending entry.

5. **Material-prior API** (stable seam for D8/D9, names fixed):
   `materials()`, `byMaterial(material)`, `byWavelengthRange(minNm, maxNm)`,
   `priorsFor(material)` (stable JSON: subclass list, per-window
   blue/green/red/redEdge/nir/swir1/swir2 reflectance statistics computed
   from the data at query time), and
   `resampleTo(SensorProfile, Library*)` reusing ADR 0079's
   `resampleSpectrumGaussian`, tagging resampled entries `resampled` and
   `sensor:<id>`; NaN outputs (band outside source coverage) fail with the
   entry and band named.

6. **Smoke gate** (tested in `tests/test_spectral_library_data.cpp`): with a
   clear-water query the two best non-self matches are water entries; with a
   healthy-vegetation query the two best are vegetation-family entries
   (`vegetation`/`cropland` — crops are physically vegetation-shaped). The
   water↔shadow↔burn and crops↔vegetation SAM ambiguities are documented
   physics, not defects; they are printed as cohesion notes by the generator.

## Consequences

- The workbench, SAM/SID matching and the D3c lab chain have real data out of
  the box; teaching uses canonical, explainable curves (green peak, red edge,
  water-vapour bands, quartz Si-O overtone doublet, ice absorption).
- D8/D9 consume stable JSON priors derived from data; future knowledge work
  needs no schema change.
- The library is redistributable by construction (CC0-1.0, zero third-party
  measured data); measured-entry ingestion is documented as a future path.
- The lenient `load` remains for user files, so validation failures cannot
  silently corrupt the workbench, and curated paths cannot silently load
  unvalidated data.
- Costs: teaching-grade synthetic curves are idealized; the cohesion notes
  and the documented ambiguities bound what SAM/SID can be expected to do
  with them. Generator maintenance is confined to one stdlib-only script.
