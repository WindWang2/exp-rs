# Recon — RS14-01 Scientific Data Passport / RemoteSensingAssetState

Baseline: `origin/master` @ `4f6632e1f` (2026-09-22, PR #1145 merged). Branch `agent/rs14-scientific-data-passport`, worktree `../exp-rs-wt-rs14-data-passport`.

## 1. Dynamic dedup (checked 2026-09-22)

- Open PRs: #1189 (RS14 agent benchmark), #1190 (RS14-18 curriculum pack), #1191 (RS14-10 unified verifier, ADR 0172), #1192 (RS14-08 capability state graph, ADR 0173), #1193 (RS14-09 task planner). **None implements an asset-level scientific state passport.** Boundary notes:
  - RS14-08 capability *graph* is about operator/tool capability transitions, not per-asset scientific state — orthogonal; our state layer must not duplicate their id space.
  - RS14-10 verifier validates *results*; we project *asset state*. Integration point recorded in docs/integration.md.
- Open issues: the full avoid-list from the prompt was re-read (current numbering #1146–#1187). Nothing in our scope requires touching them. `SICNU_SAR_STATE_ASSUMED` (#1165 context) is **projected read-only** as an `assumed` claim; no SAR behavior is changed.

## 2. Existing capabilities (facts the passport can project)

| Domain | Existing authority | Location |
|---|---|---|
| Asset identity | `AssetRecord{sourceKey, snapshot(AssetSnapshot), derivation, virtualRecipe}`; `AssetSnapshot` immutable value object (id/revision/kind/state/persistence/displayName/structure/acquisitionTime); `CatalogRecordStore` shard store, `SnapshotView::forEach/find/probe` | `src/data/catalog_record_store.h`, `src/data/data_asset.h`, `src/data/asset_types.h` |
| Band roles | `BandRole` enum (16 roles, stable lowercase strings) | `src/data/band_role.h` |
| Band structure snapshot | `RasterBandStructure{number,dataType,noDataValue,colorInterpretation,role}`, `RasterStructure{bandCount,crsWkt,geoTransform,bands}` | `src/data/data_asset.h:120-140` |
| Sensor truth registry | `SensorProfileRecord` (satellite/instrument/modality/band axis/roles/wavelengths/FWHM/GSD/calibration rule/qa vocabulary), fail-closed loader, `data/products/sensor_profiles/*.json` (ADR 0147) | `src/geospatial/products/sensor_profile.h` |
| Product discovery (Landsat/S2/MODIS/CN) | `ProductInfo/BandFile`, `landsatBandRole/sentinel2BandRole/modisBandRole`, acquisitionDate | `src/processing/algorithms/satellite_products.h` |
| Canonical GDAL metadata model | `CrsInfo`, `BandInfo` (dtype/noData/scale/offset/unit/role/wavelength/fwhm/isMask, all `has*` flags), `RasterMetadata` (sensor/platform/productId/processingLevel/acquisitionTime/radiometricState/numericScale/cloudCover/gsd/resolution) | `src/geospatial/metadata/canonical_metadata.h` (Qt-free, std::string+jsoncpp) |
| Grid compatibility | `RasterGrid`, `GridCompatVerdict` (9 verdicts), `compareGrids` | `src/data/raster_grid_compat.h` |
| Radiometric FSM (optical) | `exp_radiometric::RadiometricUnit{DN,Radiance,Toa,Boa,BT}`, key `SICNU_RADIOMETRIC_STATE` (uppercase canonical values, case-insensitive parse, missing⇒DN) (ADR 0158) | `src/core/radiometric_state.h` |
| Radiometric vocab (products) | same key, lowercase values `{radiance, toa_reflectance, surface_reflectance, brightness_temperature, digital_number}` + `SICNU_NUMERIC_SCALE` (physical = stored/scale) | `src/processing/algorithms/satellite_products.h:212-229` |
| SAR state | `SICNU_SAR_CALIBRATION` {sigma0,gamma0,beta0,dn}, `SICNU_SAR_DOMAIN` {linear_power,db}, shared `SICNU_RADIOMETRIC_STATE`; `SarStateRead{conflict}`, `SarStateCheck{Ok,OkUndeclared,Refused}`; `SICNU_SAR_STATE_ASSUMED="sigma0_legacy_undeclared"` written by geocode/terrain ops | `src/processing/algorithms/sar/sar_metadata.h` |
| Wavelength convention | per-band GDAL metadata `WAVELENGTH` + `WAVELENGTH_UNITS` (default nm; unknown units = typed refusal) | `src/processing/algorithms/spectral_wavelength.h` |
| Temporal | `AcquisitionTime{iso,precision,epochMillis,valid}`, `TemporalSceneRef{time,timeSource("explicit"\|"metadata"\|"filename"\|"descriptor"), modality, sensor, bandRoles, radiometricState, cloudCoverPercent}` (ADR 0125 forward seam) | `src/processing/algorithms/temporal/temporal_*.h` |
| Provenance | `DerivationRecord{algorithmId, algorithmVersion, parameters, inputs[DerivationInput{assetId,revision,bandReferences,valueDomain}], outputAssetId, softwareVersion, completedAtUtc, executionFingerprint, workflow*, cacheHit}` with toJson/fromJson; `DataManager::derivedFrom/derivedOutputsOf` | `src/data/derivation_record.h` |
| Sidecars | finalize manifest `x.tif.sicnu-manifest.json`; classifier `<model>.meta.json` (v1/v2, class labels/accuracy/featureSchema); uncertainty `<out>.uncertainty.json`; SAR LUT `.lut` | `src/geospatial/io/finalize_manifest.h`, `src/analysis/classification/rs_classification_pipeline.h`, `src/agent/harness/evidence.h` |
| Known/inferred/assumed/unknown/conflicted seeds | `BandRole::Unknown`, `has*` flags ("absence is meaningful"), `SarStateRead.conflict`, `SarStateCheck::OkUndeclared`, `AssetState` lifecycle enum (no conflicted/assumed), `timeSource` provenance strings | scattered, see §4 |

## 3. Extension seams for the new layer

- **Projection sources**: `AssetSnapshot` + `CatalogRecordStore::SnapshotView` (catalog), GDAL `SICNU_*` keys (file), `canonical_metadata` readers (Qt-free inspection model), `DerivationRecord` (lineage), sensor profiles (declarative truth), sidecars.
- **Test lanes**: `sicnu_add_sdk_test` = Catch2 + `sicnu_sdk` (jsoncpp only, no Qt/QGIS/GDAL) — ideal for a pure-core module. `sicnu_add_io_test` = + `Sicnu::Geospatial` (GDAL, still no Qt) — ideal for a GDAL facts collector with runtime-synthesized fixtures (repo convention).
- **CLI**: `src/cli/cli_commands.cpp` `kCommands` table + `dispatchCliCommand` chain (~L2612-2672); per-command file precedent `cli_tool_commands.{h,cpp}`; `CliIO::finish` JSON envelope. New file pair + 2 registration lines + CMake source line.
- **Agent tool**: `ToolProvider` interface (`src/agent/tool_catalog/tool_provider.h`); minimal read-only sample `data:list_layers` / closest `data:get_lineage` (input `asset_id`). `surface_registry` parity gate: MCP tools/list, CLI `tools`, `get_tool_schema` render one UNION projection — a new tool must register through the catalog so all three surfaces see it.
- **Deterministic JSON**: no shared sorted-key writer; per-projection explicit `std::sort` (`src/contracts/contract_graph.cpp:102-140`), schema id constants (`exp.scientific_contract.v1` etc.), `sicnu.labreport.v1` is the "projection of recorded truth, never computation over guesses" precedent.
- **Untrusted JSON parsing**: `Json::CharReaderBuilder` + `builder["stackLimit"]=128` + try/catch `Json::Exception` (`src/sdk/exprs/ipc_envelope.cpp:268-300`).

## 4. Gap the passport fills (why this is not a duplicate)

1. No per-asset, **versioned, claim-annotated** scientific state object exists. `AssetSnapshot` carries structure but no evidence semantics; `RasterMetadata` is a per-open GDAL dump with no claim lattice; nothing merges catalog + file + sidecar + lineage.
2. No cross-source **conflict detection** outside SAR's two-key check; the optical/SAR vocabulary split on `SICNU_RADIOMETRIC_STATE` (uppercase FSM values vs lowercase product values vs SAR calibration tokens) is unprojected.
3. "Missing ⇒ DigitalNumber" is a silent assumption in the FSM; the passport turns it into an explicit `assumed` claim (teaching value), without changing FSM behavior.
4. No **state diff** API (before/after transformation).
5. No student-facing "what is this image, what's missing, what was inferred" view.

## 5. Not doing (scope boundary)

- No SAR fixes (#1146/#1147/#1164/#1165), no DataManager/CatalogRecordStore rewrite, no replacement of metadata/provenance — projection/read-only aggregation only.
- No capability mirror changes (`data/agent/capabilities`, D8 sidecars) — new id space keyed by asset, not operator.
- No third registry/store: the passport is computed on demand, persists nothing.
- No workflow/planner/verifier/curriculum behavior (other open RS14 PRs).
- No GUI build in the inner loop; teaching surface ships as Qt-free view-model + CLI human mode, GUI wiring documented in `docs/integration.md`.

## 6. Risks

- R1: Vocabulary drift — three radiometric vocabularies on one key; mitigation: single normalization table with unit tests per synonym, `unknown` fallback, conflict claims never silently resolved.
- R2: Second-truth-source smell — mitigation: pure projection functions, no writes, no caching, documented single-source-of-truth pointers.
- R3: surface parity / capability completeness tests — adding an agent tool may trip registry-completeness gates; verify gate mechanics in Slice F before registering; fallback = CLI JSON + library API as the machine-readable surface.
- R4: build cost — core must stay jsoncpp-only (sdk-test lane); GDAL collector confined to io-test lane; Qt adapters compiled only into existing Qt targets, never in the inner loop.

## 7. Interface matrix with other tracks

| Track | They provide | We provide | Contract |
|---|---|---|---|
| RS14-08 capability graph | operator capability states | per-asset state claims | no shared types; docs/integration.md notes |
| RS14-09 planner | plan decisions | asset state query for preconditions | library API + CLI JSON |
| RS14-10 verifier | result verification | asset state evidence (known/inferred/...) | `sicnu.asset_state.v1` JSON as input option |
| RS14-17 capsule | reproducibility bundle | passport JSON export candidate | schema is versioned + deterministic |
