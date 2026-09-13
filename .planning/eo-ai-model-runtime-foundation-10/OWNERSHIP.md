# OWNERSHIP — eo-ai-model-runtime-foundation-10

## Owns (may modify)

- `src/operators/runtime/` — model runtime, providers, tile/detection engines,
  tensor blob, provenance verify, device planner, NVML inventory
- `src/operators/framework/model_catalog.*`, `model_readiness.*` — manifest truth
- `src/operators/rs/rs_inference_operator.*`, `rs_model_task_operators.*` — inference
  operator surface (task adapters)
- `src/operators/rs/rs_operators_init.cpp` — append-only registration additions
- `src/operators/CMakeLists.txt` — append-only target/option additions
- `models/*/model.json` — catalog manifests (additive; existing entries unchanged
  unless a new optional field needs a documented example)
- `data/agent/capabilities/` — model-selection knowledge projection (new files
  + additive edits to `inference.json`)
- `tests/test_model_*`, `tests/test_multimodal_inference` + new test files
- `docs/adr/` (new ADR), `docs/processing/model*` docs, `README.md` model runtime
  bullet, `CHANGELOG.md` (append-only entry)
- `.gitignore` (append-only whitelist block), `.planning/eo-ai-model-runtime-foundation-10/`

## Does NOT own (read-only; other tracks / authority)

- `src/geospatial/` — data fabric (#974) / io (io:reproject F-OPS-4 not ours)
- `src/operators/rs/rs_qa_mask_operator.*` — preprocess family (F-OPS-3 not ours)
- `src/temporal*`, temporal operators — #973
- `src/experiment/`, `src/dataset/` — MLOps foundation: integration via their
  existing public headers only; no restructuring
- `src/agent/` MCP/Pi server internals — projection via existing SpatialTool seams
  and capability JSON only
- `docs/verification/*`, `pi/exp-rs-spatial.ts` — #975
- GUI (`src/app/`) — unless a dialog registers a new task operator (avoid; CLI/MCP
  projection suffices)

## Shared-file conflict table

| File | Also touched by | Policy |
|---|---|---|
| `.gitignore` | #973/#974/#975 | append-only unique block, own commit |
| `CHANGELOG.md` | #973/#974/#975 | append own section at list head |
| `src/operators/CMakeLists.txt` | #973 | append-only blocks (sources/tests/options) |
| `src/operators/rs/rs_operators_init.cpp` | #973 | append registrations in separate integration commit |
