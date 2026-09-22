# Slices — RS14-01 Scientific Data Passport

Each slice: RED test first (fails because capability is missing, not fixture-broken) → minimal implementation → refactor → narrow green → commit → progress.md update.

## Slice A — core value types + schema round-trip (lane 1)
- `src/scientific_state/`: `asset_state_schema.h`, `asset_state_types.h`, `asset_state_json.{h,cpp}`, `CMakeLists.txt` (`sicnu_scientific_state`, jsoncpp only), root CMake +1 line.
- Tests (`tests/test_scientific_state_core.cpp`, `sicnu_add_sdk_test`): build minimal state → `toJson` has fixed key order & sorted arrays; `fromJson` round-trips (value-equal + byte-identical re-serialize); wrong/missing schema id ⇒ typed error; deterministic replay (two serializations byte-equal); invalid JSON ⇒ typed `MalformedJson`; empty vs populated boundary.
- RED check: compile fails / test fails before types exist.

## Slice B — optical projection (lane 1)
- `state_facts.h` (DatasetFacts/BandFacts/CatalogFacts/SensorProfileFacts, source tags), `asset_state_resolver.{h,cpp}` optical half: identity/kind/displayName, band roles (declared → sensor-profile axis → unknown), wavelength/FWHM nm normalization + `WAVELENGTH_UNITS` handling (unknown units ⇒ typed unknown claim, never silent), radiometric normalization table + numeric scale + FSM-default assumption + SAR token projection.
- Tests: role inference chain ordering; unit normalization (nm/µm, unknown refusal); radiometric vocabulary table (each synonym + case-insensitivity); missing marker ⇒ assumed DN with note; SAR sigma0 tokens verbatim; numeric scale projection; non-finite wavelength ⇒ unknown claim.

## Slice C — geometry / validity / temporal projection (lane 1)
- Resolver: CRS (wkt/authid/flags), geotransform (6-tuple, has-flag), pixel size + extent derivation (inferred, note), size; NoData per band + policy; quality/cloud fields; acquisition time + timeSource + precision projection; temporal refs (bounded, truncated flag).
- Tests: geotransform present/absent; extent math (incl. rotation case — projected, not normalized); missing CRS ⇒ unknown claim; cloudCover absent vs 0 (`has*` fidelity); time source tags; cap/truncation boundary (bands > 4096 ⇒ note + unknowns entry).

## Slice D — provenance + model-derived projection (lane 1)
- Resolver: DerivationFacts → provenance summary (algorithm, version, inputs w/ bandReferences, completedAtUtc, fingerprint, workflow ref, cacheHit); SidecarFacts (classifier `.meta.json`, parsed by core with stackLimit discipline) → modelDerived (labels sorted, accuracy, bandIndices, featureSchema presence).
- Tests: full derivation projection; empty inputs; sidecar v1/v2 tolerance (both accepted, foreign JSON refused w/ typed error); labels deterministically sorted; depth-bomb sidecar rejected (deep-nested fixture) without crash.

## Slice E — conflict/unknown/assumption lattice + state diff (lane 1)
- Resolver lattice completion: multi-source field merge rules (agree ⇒ known w/ merged sources; disagree ⇒ conflicted + alternatives; missing ⇒ unknown unless documented default). `asset_state_diff.{h,cpp}`: sorted field diffs, claim-kind changes, added/removed sections; deterministic bytes.
- Tests: agreement merging; each conflict shape (radiometric dual-key, role disagreement, geometry mismatch); diff before/after a simulated calibration (DN→toa_reflectance ⇒ radiometric changed, geometry unchanged); diff round-trip determinism; empty diff.

## Slice F — GDAL collector + CLI/agent surface + teaching adapter (lanes 2+)
- `src/scientific_state/gdal/gdal_state_facts.{h,cpp}` (lib `sicnu_scientific_state_gdal`, links GDAL): one read-only open → DatasetFacts (dataset+band metadata, CRS, geotransform, size, driver); also `SensorProfileFacts` adaptation from the Qt-free sensor-profile loader.
- Teaching view-model `teaching_view.{h,cpp}` (core, Qt-free): TeachingSummary + plain text; every claim kind rendered; consistency with JSON claims (same sets).
- CLI `src/cli/cli_passport_commands.{h,cpp}` + 2-line registration + CMake sources; `passport --path <file> [--json|--teaching] [--diff <passport.json>]`.
- Catalog adapter `src/scientific_state/catalog/catalog_state_facts.{h,cpp}` (lib, links Sicnu::data): AssetSnapshot/RasterStructure/DerivationRecord → facts (Qt-typed, compiled not run in inner loop).
- Agent read-only tool: verify `test_capability_surface_parity`/completeness mechanics first; register `data:asset_passport` via ToolProvider only if gates stay equally-green; else fallback documented.
- Tests: io-test lane (synthesized GTiff: declared/missing keys, per-band metadata, CRS) — RED before collector exists; teaching-view tests in lane 1; CLI smoke via lane-3 targeted run; adapter test in lane 3.

## Slice G — cross-asset fixture contract tests (lane 1 + lane 2)
- `tests/test_scientific_state_fixtures.cpp`: five families as fact fixtures (Landsat C2 L2, Sentinel-2 L2A w/ scale 10000, MODIS, SAR w/ calibration+domain+assumed-legacy, model-derived classification w/ sidecar) — assert full passport per family: claims, radiometric units, roles, provenance; agent-mode JSON vs teaching-mode summary semantic consistency; deterministic replay for all five; diff between family variants (e.g., DN vs TOA Landsat).
- One end-to-end GTiff-based case (lane 2): write a Landsat-like GTiff with SICNU_* keys → collect → resolve → JSON → round-trip → teaching summary contains the declared/inferred/assumed split.

## Final — docs + review + PR
- `docs/scientific-state/{overview,schema}.md`, `docs/scientific-state/examples/*.json` (2), `docs/integration.md` (wiring points for planner/verifier/capsule/GUI).
- Two review rounds; targeted regressions; dedup re-check; PR.
