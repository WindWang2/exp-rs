# HANDOFF — Pi-Based Spatial Intelligence Layer (ADR 0122)

**Date:** 2026-08-24
**Mode:** FULL_AUTONOMOUS — analyze → worktree → implement → review loop → test → PR
**Branch:** `feature/pi-spatial-intelligence-layer` → **PR #476**
**Prior session handoffs:** preserved in git history (last: 2026-08-08 optical
platform session, ADRs 0065–0119, 60 vertical slices).

---

## 1. What This Session Delivered

exp-rs is now the **spatial capability provider for the Pi agent runtime**
(pi.dev, `@earendil-works/pi-coding-agent`); Pi owns the agent loop /
planning / memory / reasoning. That division of responsibility is the core
principle of ADR 0122 (`docs/adr/0122-pi-spatial-intelligence-layer.md`).

| Piece | Where |
|---|---|
| `SpatialTool` contract + registry (`name/description/input_schema/execute/output_schema`) | `src/agent/spatial_tools/spatial_tool.{h,cpp}` |
| `spatial:raster_inspect` (GDAL metadata, band roles, wavelengths, nodata, radiometric state, subsampled stats) | `src/agent/spatial_tools/raster_inspect_tool.*` |
| `spatial:vector_inspect` (layers, schemas, extents, sampled features → GeoJSON) | `src/agent/spatial_tools/vector_inspect_tool.*` |
| `spatial:list_models` + `ModelCatalog` (`models/*/model.json`); `rs:infer` name resolution | `src/agent/spatial_tools/model_catalog_tool.*`, `src/operators/framework/model_catalog.*` |
| MCP `run_workflow` / `get_workflow_status` (agent DAG → `TaskCenter::submitPipelineJson`) | `src/agent/mcp_server.{h,cpp}` |
| MCP `tools/list` enumerates the unified catalog (algorithms + interaction + data + spatial) with full schemas | `src/agent/mcp_server.cpp` (`handleRequest` tools/list) |
| Algorithm capability sidecars (`task/input/output/gpu/accuracy`) + `AlgorithmMetaStore` overlay | `data/processing/algorithm_meta/*.json`, `src/processing/framework/algorithm_meta_store.*` |
| Pi extension (spawn `--mcp`, handshake, dynamic tool registration, abort-aware, truncation, `wait_for_execution`) | `pi/exp-rs-spatial.ts` |
| Agent knowledge base (task → algorithm selection guide) | `pi/knowledge/spatial-algorithm-guide.md` |

`spatial:` joined the MCP allow-list (inline, read-only execution — long work
still goes through `ToolCallDispatcher` → TaskCenter with provenance).

## 2. Verification

- `test_spatial_tools` — 8 cases / 83 assertions, all pass (runtime-generated
  GTiff/GeoJSON fixtures, catalog + meta-store coverage).
- `test_mcp_server` — full suite green: 13 cases / 772 assertions (incl. 3
  new cases: run_workflow end-to-end through TaskCenter, malformed-pipeline
  rejection, spatial tools/call dispatch + tools/list contents).
- `test_agent_tool_catalog` (1145) and `test_tool_call_dispatcher` (152) —
  green, no regression.
- **E2E smoke against the real `sicnu_geo_rs --mcp` binary**: handshake,
  179 tools listed, model catalog resolution, all 6 sidecars attached,
  workflow submit + aggregate status, error paths — 10/10 checks.
- Review loop round 1 fixed 9 findings, notably: `SpatialToolRegistry::reset()`
  self-deadlock, `rs:infer` catalog lazy-load, QMap iterator misuse
  (`.value()`, not `->second`), GDAL API fixes (`exportToWkt` casing,
  `exportToJson` return-value semantics), a model manifest path bug, and TS
  bridge exit-flag/EPIPE/abort races.

## 3. Build Notes (this machine)

- The ZCode AppImage pollutes `LD_LIBRARY_PATH`, which breaks CMake's
  self-location — **prefix every configure/build command with
  `env -u LD_LIBRARY_PATH`**.
- System GCC 16.2.1 (dev snapshot) randomly ICEs under load and can leave
  corrupt `.o` files that later fail the link with
  `reloc against .debug_str`. The recipe that worked: `ninja -k 0 -j4` in a
  retry loop that deletes object files flagged `internal compiler error`
  and rebuilds.
- Worktree build: `cmake -G Ninja --preset dev-default` (the generator must
  be explicit; preset defaults differ from the main tree's Ninja cache).

## 4. Documentation State (synced this session)

- `README.md` — Spatial Intelligence section, corrected test count (1,758
  Catch2 cases defined in `tests/`), current architecture tree (`agent/`,
  `pi/`, `models/`, `algorithm_meta/`).
- `CLAUDE.md` — architecture map + Language note (the only non-C++ is the
  `pi/` TypeScript adapter).
- `CONTEXT.md` — new domain terms (Spatial Tool, Spatial Tool Registry,
  Model Catalog, Algorithm Capability Sidecar, Pi Bridge), updated MCP Server
  term, ADR 0062–0122 title index.
- `docs/repo-layout.md` — `pi/`, `models/`, `docs/adr/`,
  `data/processing/algorithm_meta/`, `src/agent/spatial_tools/` rows.
- `pi/README.md`, `models/README.md`,
  `data/processing/algorithm_meta/README.md`, `CHANGELOG.md` — added with the
  feature.
- Historical session logs (`progress.md`, `task_plan.md`, `findings.md`,
  `docs/agent/*`) intentionally left as records of their own sessions.

## 5. Known Follow-ups

- Copilot-side `spatial:` dispatch (currently MCP/Pi-facing only).
- Streaming tile cache for raster tiles / embeddings; GPU-batch execution
  queue; automatic model selection from `ModelCatalog`.
- Package `pi/` as an installable Pi package (`pi install`).
- `master` still lacks everything above until PR #476 merges.
