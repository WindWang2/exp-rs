# OWNERSHIP — scientific-contract-verification-10

## Write scope (exclusive this track)

- `src/contracts/` — new scientific contract layer (additive files) + graph_assembly extension
- `src/runtime/observability/` — only if readiness/trace integration needs a seam (not planned by default)
- `scripts/collect_readiness.py`, `scripts/verification_ladder.py` — additive lanes
- `tests/test_scientific_contract_10.cpp`, `tests/test_drift_projection_10.cpp`, `tests/test_science_verification_10.cpp` (new), `tests/CMakeLists.txt` (append-only registrations)
- `review/` — findings disposition ledger updates (`review/findings/`, new `review/track10_dispositions.md`)
- `docs/verification/` — READINESS refresh, `SCIENTIFIC_CONTRACT_10.md`
- `data/contracts/` — regenerated snapshot (conscious-diff policy per CONTRACT_PLATFORM_9.md)
- `.planning/scientific-contract-verification-10/`
- Narrow bugfix scope (findings): `src/operators/io/io_operators.cpp`, `src/geospatial/convert/raster_convert.{h,cpp}`, `src/operators/framework/model_catalog.cpp`, `src/operators/runtime/tile_inference_engine.cpp`, `src/operators/rs/rs_qa_mask_operator.cpp`, `src/operators/runtime/detection_postprocess.{h,cpp}`, `src/operators/runtime/detection_tile_engine.cpp`, `src/operators/runtime/tensor_blob.cpp`, `pi/mcp_bridge.ts`, `pi/exp-rs-spatial.ts`, `pi/test/*`

## Read-only (owned by others)

- `src/operators/rs/**` (except rs_qa_mask_operator.cpp narrow fix) — scientific-algorithms track
- `src/processing/`, `src/agent/`, `src/app/`, `src/gui/`, `src/analysis/` — UI/agent tracks
- `data/processing/algorithm_meta/**` — content owned by capability tracks; Track 10 reads it for drift gates and only *reports* drift (registry of missing entries goes to test allow-lists with reasons, not mass content authoring)
- `labs/` / LabSpec documents — lab tracks; Track 10 reads for drift gates
- `master` branch — read-only always

## Cross-track seam policy

- If a drift gate needs content another track owns, the gate fails-with-reason + allow-list entry carrying `OWNED-BY-<track>`; never silently green, never mass-edit their files.
- Shared registries (CMake, tests/CMakeLists.txt): append-only, separate integration commits.
