# ARCHITECTURE — eo-ai-model-runtime-foundation-10

## Principle
One execution seam (`runModelInference` → `TileInferenceEngine`/`DetectionTileEngine`)
remains the only inference path. 10.0 adds: (a) a typed EO truth layer on the manifest,
(b) task-shaped adapters that project INTENT onto that seam (same as 4.0's
rs:segment/rs:detect/rs:embedding), (c) optional providers behind the existing
`ModelRuntimeRegistry::registerProvider` seam, (d) knowledge projections that read
manifest facts — never new inference implementations.

## Key contracts introduced
- **EO task vocabulary**: closed set with legacy alias acceptance; drives adapter
  defaults + agent knowledge. `task` remains the single source; no parallel taxonomy.
- **`eo` manifest section**: additive, optional, version-gated (declared manifest
  version bumps to 6 when used). Fields: `wavelength_nm` per band role
  ({"nir": [780, 1400]}), `calibration` requirement ({"radiometric_state":
  "toa_reflectance", "required": true}), `grid` assumption ({"crs_family": "utm",
  "alignment": "reference"}). Runtime consumes wavelengths/calibration as typed
  preflight checks (refuse when input metadata contradicts a REQUIRED calibration).
- **Typed task artifacts**: every task operator result carries
  `artifact: {kind, path, schema_version, ...}` — classification: JSON class record;
  change: raster mask + optional JSON transition stats; regression: continuous raster;
  instances: vector file + per-instance attributes.
- **Resume seam**: `tiling.resume` (default off) → `<output>.resume.json` checkpoint
  {plan fingerprint, completed tiles[]}; engine skips completed tiles; checkpoint
  removed on success; provenance records resumed=true + checkpoint digest.
- **Optional providers**: adapters in `src/operators/runtime/`, compiled under
  `SICNU_ENABLE_TENSORRT` / `SICNU_ENABLE_OPENVINO` (auto-detect deps; OFF ⇒ symbol
  absent, registry reports typed DeviceUnavailable). Default build artifact set
  identical to master.
