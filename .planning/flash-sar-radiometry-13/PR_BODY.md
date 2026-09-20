# feat(sar): Radiometric State & Calibration Chain 13.0 — census, derived products, LUT calibration, E2E provenance

**Track**: `flash-sar-radiometry-13` (GOAL Loop, autonomous, GLM-5.3-Flash)
**Baseline**: `79adfe78a16b9419eef180cf9e6e5739658621a2` (origin/master at fetch time; no drift at PR time — re-fetched and re-scanned)
**Scope**: SAR radiometric state contract hardening. No new operators, no registry/ABI change; one additive capability-sidecar row for a new schema parameter.

## Real-time dedup result (Phase 0 + re-checks at each milestone)

- Open PRs at fetch time (3): #1137 geospatial-maintenance-13 (no SAR files), #1136 offline-labs-13 (`data/labs/lab12_sar_processing.lab.json` only), #1135 temporal-phenology-12 (`rs_temporal_sar_fusion_operator.*` + its capability sidecar). Open issues: 0. None intersects this branch's files (verified per-PR with `gh pr view --json files` and again at PR time).
- Predecessor: PR #1131 (SAR 12.0) closed the re-calibration guards in `rs:sar_calibrate`/`rs:sar_backscatter` and speckle state propagation, and recorded its own follow-ups verbatim: terrain operators hardcode gamma0, geocode never writes `SICNU_RADIOMETRIC_STATE`, ratio/texture drop the state, DN LUT calibration out of scope. This branch implements exactly those gaps after an independent census of all 23 registered `rs:sar_*` operators.
- No live PR implements any of these gaps; no pivot was needed. Temporal SAR fusion (#1135) and lab data (#1136) are explicitly out of ownership and untouched.

## Design

1. **Census** (`tests/test_sar_radiometric_state.cpp`): a machine-readable table gives every registered `rs:sar_*` operator a state rule or an explicit exemption with the reason recorded. The gate enumerates `RSOperatorRegistry::instance().operatorNames()` and fails on an unclassified operator or a stale table row. 8 first-class radiometric operators + 15 recorded exemptions (complex/phase, geometry/displacement, polarimetric/dual-pol features, masks, temporal aggregates).
2. **Derived product tokens** (additive): `rs:sar_ratio` declares `sar_pair_metric`, `rs:sar_texture` declares `sar_texture` in both `SICNU_RADIOMETRIC_STATE` and `SICNU_SAR_CALIBRATION`. `normalizeCalibration()` maps them to `""`, so the existing fail-closed guards reject them (with a precise derived-product message) — a pair metric or GLCM measure can never re-ingest as backscatter. `rs:sar_speckle` propagates them so filtering a derived product does not drop its state.
3. **Terrain family input contract**: `rs:sar_terrain_flatten` / `rs:sar_terrain_correction` apply `sigma0·cosθ0/cosθi`, lawful only for sigma0 input. Declared `gamma0`/`beta0`/`dn`/derived/conflicting/unrecognized states are typed refusals before any output is created; legacy undeclared scenes are accepted with a logged warning and the assumption persisted as `SICNU_SAR_STATE_ASSUMED=sigma0_legacy_undeclared` (a log line does not travel with the artifact).
4. **Geocode state block**: same declared-sigma0 contract plus the db-domain refusal (its gamma0 band applies `sin(θL)/sin(θ0)`, Ulander 1996 / Small 2011 eq. 5). The output now writes the full state block and the additive per-band map `SICNU_SAR_GEOCODE_BAND_STATES = sigma0,gamma0,incidence_deg,local_incidence_deg,mask_class` (the product is inherently mixed; one dataset token cannot describe five bands). `result.bandStates` mirrors the key.
5. **DN LUT calibration** (`rs:sar_calibrate`): a per-row calibration LUT — plain-text sidecar with exactly one finite, positive constant per input row, referenced by the `calibrationLut` parameter (verbatim) or the declared `SICNU_SAR_CALIBRATION_LUT` metadata (raster-relative, confined to the raster's directory). `sigma0 = (DN² − noiseLinear)/A(row)²`, same NoData policies as the constant path. Interpolation is defined as *none*: a count mismatch, an unreadable file, a non-numeric or non-positive entry, or an escaping path is a typed refusal; the constant A is never substituted for an unreadable contract. Annotation-XML LUTs are not parsed (documented unsupported; the GF-3 adapter likewise invents no constants, ADR 0159).
6. **gamma0 vocabulary** (documented): within the terrain/geocode family the token means *terrain-flattened gamma0*; `rs:sar_backscatter`'s gamma0 is a pure geometric normalization and must be converted back with `gamma0ToSigma0` before entering the family. The geocode refusal is fail-closed for every gamma0 flavor.
7. **E2E provenance**: `import (DN) → calibrate → speckle → terrain_flatten (→ gamma0) → ratio` plus a geocode branch, asserting the declared state after every step, refusing double calibration / geocoding a gamma0 product / mixed-state or derived ratio inputs / conflicting declarations / missing-malformed-mismatched LUTs (each with no partial output), and reopening every chain output to confirm the metadata survives.

## Test commands & results (two consecutive passes, `-j2` builds, `-j1` ctest, offscreen Qt)

Targets: `test_sar_radiometric_state test_sar_operators test_sar_kernels test_speckle_filter test_sar_polsar test_sar_insar test_sar_complex test_sar_geocoding test_drift_projection_10` (`cmake --build build-dev -j2 --target …`, exit 0, 0 errors).

| suite | pass 1 | pass 2 |
|---|---|---|
| test_sar_radiometric_state | All tests passed (373 assertions, 14 cases) | identical |
| test_sar_operators | All tests passed (675 assertions, 26 cases) | identical |
| test_sar_kernels | All tests passed (89 assertions, 12 cases) | identical |
| test_speckle_filter | All tests passed (15869 assertions, 26 cases) | identical |
| test_sar_polsar | All tests passed (143 assertions, 8 cases) | identical |
| test_sar_insar | All tests passed (266 assertions, 10 cases) | identical |
| test_sar_complex | All tests passed (378 assertions, 7 cases) | identical |
| test_sar_geocoding | All tests passed (5769 assertions, 8 cases) | identical |
| test_drift_projection_10 | All tests passed (2733 assertions, 3 cases) | identical |

**26,999 assertions / 107 cases green on both passes.** (test_sar_radiometric_state: 373 assertions / 14 cases, incl. the assumed-state provenance, potent traversal, unmasked ratio-conflict, no-trailing-newline LUT and message-text assertions.)

**Red evidence**: with the branch's `src/` + sidecar stashed (pristine master code, same test binary), the new gate fails 8/12 cases at the first commit point; after remediation the two band-states assertions fail on pristine sources. **Mutation potency**: deleting `terrain_flatten`'s radiometric-state write fails 3 cases (census behavioral gate, terrain legacy test, E2E readback); deleting `ratio`'s derived-state writes fails 2 cases (census behavioral gate, E2E readback). Both mutations were reverted and re-verified green.

## Independent review

Two read-only deep reviews by a reviewer agent separate from the implementer (subagents never ran builds). First pass: **0 P0 / 1 P1 / 3 P2 / 9 P3**, all fixed in `1d4a34752` + `476260e80`:

- **P1** — the new `calibrationLut` schema parameter broke `test_drift_projection_10`'s schema↔sidecar parameter projection (green on master): fixed by adding the sidecar row to `data/processing/algorithm_meta/capability/rs-sar-calibrate.json` (that file is hand-maintained, not generated by `generateCatalog`, so the edit is stable). Gate re-run: 2733/3 green.
- **P2** — unbounded `readAll()` on the data-controlled LUT path: replaced by streamed parsing with a size pre-check and an early exit once the row budget is exceeded.
- **P2** — geocode stamped a `sigma0` claim on legacy undeclared inputs with only a log warning: the assumption is now persisted as `SICNU_SAR_STATE_ASSUMED` (same for the terrain operators).
- **P2** — `gamma0` had two incompatible meanings in one vocabulary: documented (terrain-flattened vs backscatter's geometric normalization), the geocode refusal message and the E2E comment corrected.
- **P3s** — declared-LUT directory containment (traversal refused), `calibrationA` validated only on the constant path, multi-band LUT warning, per-band states key distinguishes the two incidence bands, refusal messages name the actual key and distinguish undeclared from unrecognized tokens, plus new tests for the previously unexercised ratio refusals and LUT edge cases (precedence, containment, count direction, 0-byte).

Second-pass review of the remediation: **SHIP** — P1 and both P2s verified genuinely fixed; the remaining 10 P3s (test potency, containment hardening, LUT byte budget / stream status, coverage gaps, doc wording) were fixed in `ef8685193` and re-verified with two more consecutive green passes. Review log: `.planning/flash-sar-radiometry-13/`.

## Known limitations (pre-existing red gates, unrelated to this branch)

Reproduced identically with this branch's `src/` stashed (pristine master), so they are neither caused nor masked here: `test_algorithm_meta_drift` does not compile on master (a literal newline inside a character literal in `lfOnly`), `test_capability_drift` (3 cases: cartography/io/terrain-landform/solar-geometry/gaofen coverage + the `optical_ndvi_landsat` alias recipe), `test_capability_surface_parity` (1), `test_catalog_size` (1: the full-catalog envelope grew past its 176 KiB budget from unrelated merged tracks). This branch touches no recipes, capability projections other than the one documented row, or agent code.

## Resource discipline

`-j2` builds only (load ≤ 18 on 40 cores, RSS < 40%), `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`, loopback-free synthetic fixtures, no full rebuild per iteration (target-scoped only), no CI wait, no PR self-merge.

## Conflict hotspots

`tests/CMakeLists.txt` (append-only convention — the open PRs also append there; textual conflict only), `.goal-loop-ledger.md` and `.planning/` (every track appends). `docs/processing/sar-domain.md` §16 follows §15 from SAR 12.0.
