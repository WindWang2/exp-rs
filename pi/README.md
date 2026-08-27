# exp-rs × Pi — Spatial Intelligence Bridge (ADR 0122)

This directory is the **pi-adapter** layer: it turns exp-rs into the spatial
capability provider for the [Pi agent runtime](https://pi.dev)
(`@earendil-works/pi-coding-agent`).

```
Pi (agent loop · planning · memory · reasoning)
 │  TypeScript extension (this directory)
 ▼
exp-rs --mcp (stdio JSON-RPC)
 │
 ├─ spatial:* inspection tools      (raster/vector metadata, model catalog)
 ├─ discovery meta tools            (search_algorithms, get_algorithm_schema, …)
 ├─ execution                       (execute_algorithm → TaskCenter, provenance)
 ├─ workflows                       (run_workflow → DAG, get_workflow_status)
 └─ models                          (spatial:list_models, rs:infer by name)
```

Pi intentionally ships no MCP client; the extension therefore owns the
transport (spawn + handshake + `tools/list` → `pi.registerTool`). See
`docs/adr/0122-pi-spatial-intelligence-layer.md` for the decision record.

## Quick start

```bash
# 1. Build exp-rs (desktop binary carries the MCP server)
cmake --preset dev-default && cmake --build build-dev

# 2. Load the extension in Pi
pi -e pi/exp-rs-spatial.ts

# 3. Ask for a spatial task, e.g.
#    "Inspect data/samples/landsat_sample.tif, then compute NDVI and
#     polygonize areas above 0.6 as a workflow."
```

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `EXP_RS_MCP_BIN` | auto-detect (`build/sicnu_geo_rs`, `build-dev/…`) | Binary launched with `--mcp` |
| `EXP_RS_MCP_ARGS` | — | Extra CLI args for the server |
| `EXP_RS_TOOL_CATEGORIES` | `meta,spatial,data` | Tool prefixes to bridge (case-insensitive; add `rs,gdal,gdal_tools,otb_tools,qgis_algorithms,opencv,view,raster` for direct algorithm tools — matching the MCP allow-list prefixes) |
| `SICNU_MCP_WORKSPACE` | — | Restrict server file access to this root |
| `SICNU_MODELS_DIR` | `<repo>/models` | Model manifest catalog root |

Tool names are sanitized for provider limits (`rs:spectral_index` →
`exprs_rs_spectral_index`); a mapping table is unnecessary because the
original id is included in each tool description.

## What the agent gets

- **Discovery**: `exprs_search_algorithms` / `exprs_list_algorithms`
  (catalog sidecars in `data/processing/algorithm_meta/` enrich supported
  algorithms with task/input/output/gpu metadata; coverage is incremental,
  not every catalog entry has one yet).
- **Understanding**: `exprs_spatial_raster_inspect`,
  `exprs_spatial_vector_inspect` (band roles, wavelengths, radiometric
  state, field schemas, sampled features).
- **Planning aid**: `exprs_preflight_algorithm` (schema + grid + RAM
  estimate, no execution).
- **Execution**: `exprs_execute_algorithm` → `execution_id` → poll
  `exprs_get_execution_status` or block in `exprs_wait_for_execution`.
- **Workflows**: `exprs_run_workflow` submits an agent-generated pipeline
  DAG through the Task Center (per-step execution ids, aggregate status via
  `exprs_get_workflow_status`).
- **Models**: `exprs_spatial_list_models`; `rs:infer` accepts catalog names.

## Knowledge for agents

`knowledge/` holds the task→algorithm selection guide. Pi skills or the
session prompt can point the model at
`pi/knowledge/spatial-algorithm-guide.md` before planning.

## Files

- `exp-rs-spatial.ts` — the extension (dependency-free TypeScript, loaded
  by Pi via jiti; no build step).
- `knowledge/spatial-algorithm-guide.md` — algorithm selection knowledge.
