# ADR 0122: Pi-Based Spatial Intelligence Layer

## Status

Proposed

## Context

exp-rs has grown a strong spatial capability core: a Processing Registry with
70+ algorithms (`AtomicAlgorithmRegistry`), TaskCenter DAG execution, a unified
`AgentToolCatalog`, an OpenAI-compatible copilot, and a stdio MCP server. What
it lacks is a **runtime-independent way for external agent harnesses** —
specifically [Pi](https://pi.dev) (the TypeScript agent harness from
Earendil/badlogic, npm `@earendil-works/pi-coding-agent`) — to drive the whole
spatial stack: understand data, discover algorithms, compose workflows, run
them, and inspect models.

Pi deliberately ships **no MCP client support** ("No MCP", pi.dev). Pi
integrates foreign capabilities through **TypeScript extensions** that register
tools via `pi.registerTool()`. Therefore a Pi adapter cannot be "just config":
it must be an extension that owns the transport itself.

Key constraints from the mission:

1. Do not rebuild agent infrastructure (loop, planning, memory, reasoning stay
   in Pi; exp-rs stays the spatial capability provider).
2. Prefer extending existing seams (`AgentToolCatalog`, `ToolProvider`, MCP
   server, `TaskCenter::submitPipelineJson`) over parallel structures.
3. No new heavyweight dependencies (no YAML parser; manifests stay JSON,
   matching `data/tools/custom/*.json` and `toolbox_manifest.json` precedent).

## Decision

Add a Pi-oriented spatial intelligence layer in five pieces, all additive:

### 1. `SpatialTool` interface + registry (`src/agent/spatial_tools/`)

A single executable abstraction — the mission's `SpatialTool` contract:

```
name() / description() / inputSchema() / outputSchema() / execute(input)
```

`SpatialToolRegistry` (process singleton) holds implementations.
`SpatialToolProvider` bridges them into the existing `AgentToolCatalog` as a
`ToolProvider`, so the copilot, `--list-tools`, and MCP `tools/list` all see
them with full JSON Schemas — no new catalog path.

First tools: `spatial:raster_inspect` (GDAL metadata, band roles, wavelengths,
nodata, optional statistics, radiometric state) and `spatial:vector_inspect`
(OGR layer schema, extent, CRS, optional sampled features). They are
read-only and synchronous, so the MCP server executes them inline — unlike
algorithm calls, which keep flowing through `ToolCallDispatcher` → TaskCenter.

### 2. Workflow tools for agents (MCP meta tools)

`run_workflow` submits an agent-generated pipeline JSON through the existing
`TaskCenter::submitPipelineJson` seam; `get_workflow_status` aggregates the
per-step task statuses via `getPipelineInfo`. This is the "Spatial Workflow
Graph" entry point: Pi plans the DAG, exp-rs executes it with provenance.

### 3. Algorithm metadata sidecars (`data/processing/algorithm_meta/`)

Optional per-algorithm JSON manifests (`id`, `task`, `input`, `output`, `gpu`,
`accuracy`, `notes`, `tags`) loaded by `AlgorithmMetaStore`
(`src/processing/framework/`) and merged into MCP `list_algorithms`,
`search_algorithms`, and `get_algorithm_schema` responses as a `catalog`
object. Descriptors stay untouched; the store is a pure overlay that agents
use for capability discovery (task→algorithm matching).

### 4. Model runtime catalog (`src/operators/framework/model_catalog/`)

`ModelCatalog` scans `models/*/model.json` (name, task, input/output contract,
framework, GPU requirement, accuracy, optional local weight path). Exposed via
the `spatial:list_models` tool; `rs:inference` now resolves a catalog model
**name** to its path (path behavior unchanged). No weights are shipped; the
catalog documents what can be plugged in and gives future model-selection
logic something to read.

### 5. Pi adapter (`pi/`)

`pi/exp-rs-spatial.ts` — a dependency-free TypeScript Pi extension that:
spawns the exp-rs binary with `--mcp`, performs the MCP handshake
(`2024-11-05`), lists tools, and registers each as a Pi tool (name-sanitized,
schema passed through, results truncated, abort-aware). Meta tools
(`search_algorithms`, `execute_algorithm`, `run_workflow`, …) compose the
whole stack; an extra `exprs_wait_for_execution` convenience tool polls
`get_execution_status` until a run finishes. `pi/knowledge/` holds the
task→algorithm selection guide agents read before planning.

Division of responsibility (the mission's core principle):

| Concern | Owner |
|---|---|
| Agent loop, planning, memory, reasoning | Pi |
| Spatial data understanding | `spatial:*` tools + data tools |
| Algorithm discovery/selection | catalog + `algorithm_meta` overlay + knowledge docs |
| Workflow composition | Pi (generates pipeline JSON) |
| Workflow execution | `TaskCenter` DAG via `run_workflow` |
| Model management | `ModelCatalog` + `rs:inference` |

## Alternatives considered

- **Build an agent loop inside exp-rs** — rejected: duplicates Pi; the
  existing copilot already covers the embedded-LLM niche.
- **Wait for Pi MCP support / use another harness** — rejected: extension API
  is stable and documented; the bridge is ~300 lines and runtime-only.
- **YAML algorithm manifests** — rejected: repo is JSON-first and vendoring a
  YAML parser for sidecar cosmetics is not worth a new dependency.
- **Execute spatial tools through TaskCenter** — rejected for v1: inspection
  tools are fast and read-only; inline execution keeps the agent loop simple.
  Long-running work stays in TaskCenter.

## Consequences

- MCP `tools/list` now also exposes catalog tools (algorithms, interaction,
  data, spatial) with full schemas, so any MCP-capable client benefits — the
  Pi extension is a consumer of this, not a privileged caller.
- The `spatial:` prefix joins the MCP allow-list; custom tools remain gated.
- `sicnu_agent` gains a link on `sicnu_operators` (for `ModelCatalog`); the
  dependency direction (agent → operators → processing) stays acyclic.
- Tests: `tests/test_spatial_tools.cpp` covers the registry, both inspect
  tools (runtime-generated GeoTIFF/GeoJSON fixtures), the model catalog, and
  the metadata store; `test_mcp_server` target compiles the new server code.
- Follow-ups (not in this slice): streaming tile cache for embeddings,
  GPU-batch execution queue, richer model auto-selection, copilot-side
  `spatial:` dispatch (currently MCP/Pi-facing).
