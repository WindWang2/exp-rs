# exp-rs × Pi

This directory holds two integrations with the [Pi agent runtime](https://pi.dev)
(`@earendil-works/pi-coding-agent`). They are not interchangeable.

1. **In-product copilot (vendored foundation).** The desktop Agent dock can
   run a packaged Node process plus the locked Pi 1.5.2 SDK. The wrapper
   lives in `product-runtime/`. Tools are admitted and executed only by the
   Qt `AgentToolHost` over the private `sicnu.pi/1` protocol. This path does
   **not** spawn `exp-rs --mcp`, does not embed the Pi terminal UI, and does
   not require the product user to install Pi, Node, or npm. See
   `docs/development/pi-product-agent/`.
2. **External Pi CLI extension (ADR 0122).** `exp-rs-spatial.ts` is a
   dependency-free extension for a standalone Pi CLI. It spawns the desktop
   binary with `--mcp` and registers server tools with that Pi session. The
   `--mcp` process has its own in-memory DataManager and Task Center; it is
   not the GUI execution authority.

Upstream Pi now documents built-in MCP and Codemode. The in-product runtime
disables those built-ins (no Shell, arbitrary file I/O, arbitrary
extensions, or Codemode). The external extension below still owns its own
stdio transport so a standalone Pi CLI can drive exp-rs without relying on
Pi's built-in MCP client. The original no-MCP-client wording in
[ADR 0122](../docs/adr/0122-pi-spatial-intelligence-layer.md) is historical;
that ADR now points here and to the 2026-11-15 product plan for the current
constraint.

The rest of this file documents the **external** extension only.

```
standalone Pi CLI (agent loop · planning · memory · reasoning)
 │  TypeScript extension (exp-rs-spatial.ts)
 ▼
exp-rs --mcp (stdio JSON-RPC)   // separate process, not the GUI host
 │
 ├─ spatial:* inspection tools      (raster/vector metadata, model catalog)
 ├─ discovery meta tools            (search_algorithms, get_algorithm_schema, …)
 ├─ execution                       (execute_algorithm → TaskCenter, provenance)
 ├─ workflows                       (run_workflow → DAG, get_workflow_status)
 └─ models                          (spatial:list_models, rs:infer by name)
```

## External extension: quick start

```bash
# 1. Build exp-rs (desktop binary carries the MCP server)
cmake --preset dev-default && cmake --build build-dev

# 2. Load the extension in a standalone Pi CLI
pi -e pi/exp-rs-spatial.ts

# 3. Ask for a spatial task, e.g.
#    "Inspect data/samples/landsat_sample.tif, then compute NDVI and
#     polygonize areas above 0.6 as a workflow."
```

Do not use this command line to reason about the in-product copilot. The
product path is started by Qt from the bundled Node and `product-runtime`.

## External extension: configuration

| Variable | Default | Meaning |
|---|---|---|
| `EXP_RS_MCP_BIN` | auto-detect (`build/sicnu_geo_rs`, `build-dev/…`) | Binary launched with `--mcp` |
| `EXP_RS_MCP_ARGS` | — | Extra CLI args for the server |
| `EXP_RS_TOOL_CATEGORIES` | `meta,spatial,data,temporal,cartography,symbology,workflow,workspace,layout,harness,mission` | Tool prefixes to bridge (case-insensitive). Opt-in families beyond the default: `scientific` (durable scientific agent sessions — `scientific:agent_session` / `scientific:context` / `scientific:capabilities`), and direct algorithm tools `rs,gdal,gdal_tools,otb_tools,qgis_algorithms,opencv,view,raster` (matching the MCP allow-list prefixes). `exprs_status` reports what the current session bridged plus how to opt in. |
| `SICNU_MCP_WORKSPACE` | Pi's working directory | Sandbox root for server file access (default-deny: unset → the directory Pi runs in, or `~/.exp-rs/workspace` when that is `/` or `$HOME`; set `/` to widen deliberately) |
| `SICNU_MODELS_DIR` | `<repo>/models` | Model manifest catalog root |

Tool names are sanitized for provider limits (`rs:spectral_index` →
`exprs_rs_spectral_index`); a mapping table is unnecessary because the
original id is included in each tool description.

## What the external agent gets

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
- **Scientific sessions (opt-in)**: set
  `EXP_RS_TOOL_CATEGORIES=…,scientific` to bridge
  `exprs_scientific_agent_session` — durable plan→preflight→execute→verify
  sessions over the same production engine the CLI `session` command and the
  desktop Control Center drive (journals, resume, reconcile, typed refusals).
- **Models**: `exprs_spatial_list_models`; `rs:infer` accepts catalog names.
- **Bridge status**: `exprs_status` (always registered) reports live
  connectivity, server generation/protocol, schema-level catalog drift after
  a respawn, rejected name collisions, unmet dependencies, and next actions.

## Knowledge for agents

`knowledge/` holds the task→algorithm selection guide. Pi skills or the
session prompt can point the model at
`pi/knowledge/spatial-algorithm-guide.md` before planning.

## Files

- `product-runtime/` — in-product controlled Pi session, private protocol,
  and checkpoint modules used by the vendored Qt sidecar. See
  `docs/development/pi-product-agent/`.
- `exp-rs-spatial.ts` — the external extension shell (dependency-free
  TypeScript, loaded by a standalone Pi CLI via jiti; no build step). The
  transport lives ONLY in `mcp_bridge.ts` — the shell imports it, and
  `test/no_drift.test.mjs` fails if a second McpBridge copy, constants, or
  teardown hook reappear (Compiler 10.0 drift guard for review findings
  F-PI-1/F-PI-2).
- `mcp_bridge.ts` — the single newline-delimited JSON-RPC transport for the
  external `--mcp` path: lifecycle, cancellation, circuit breaker, and the
  F-PI-1 overflow recovery (a runaway line kills the child so the next
  request lazy-respawns).
- `knowledge/spatial-algorithm-guide.md` — algorithm selection knowledge.
- `roles/` — prompt-only subagent role cards; they are not a second
  execution authority.
