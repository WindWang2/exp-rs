# Pi Adapter Architecture (Harness 4.0)

> Pi is the generic agent foundation; ExpRS implements the geospatial /
> remote-sensing harness and domain tools.

## Components

```
pi/exp-rs-spatial.ts      dependency-free Pi extension (the adapter)
pi/mcp_bridge.ts          stdio JSON-RPC transport (Pi ships no MCP client)
pi/knowledge/             task→algorithm selection guide agents read
pi/roles/                 subagent role cards (review/cross-check only)
```

## Bootstrap

1. Pi loads the extension's default export.
2. The adapter locates the desktop binary (`EXP_RS_MCP_BIN` or the build tree)
   and spawns it with `--mcp` over stdio.
3. JSON-RPC handshake (`2024-11-05`), 30 s startup deadline.
4. `tools/list` with schemas; tools are filtered by
   `EXP_RS_TOOL_CATEGORIES` (default:
   `meta,spatial,data,temporal,cartography,symbology,workflow,workspace,layout,harness`)
   and registered as Pi tools named `exprs_<sanitized>`.
5. Tool calls dispatch over the bridge; results are truncated to the last
   50,000 chars; abort maps to `notifications/cancelled` → `TaskCenter::cancelTask`.

`harness:` is the 4.0 addition: preflight, plan, execute, run status, context,
manifests, errors, and recipes reach Pi as first-class tools. Staged
discovery is the documented pattern: `search_tools` → `get_tool_schema` /
`harness:tool_manifest` → invoke.

## Division of responsibility

| Concern | Owner |
|---|---|
| Agent loop, planning, memory, reasoning | Pi |
| Subagent orchestration | Pi (`pi/roles/` cards; single-writer rule) |
| Spatial data understanding | `spatial:*` + `data:understand` grounding |
| Scientific safety | `harness:preflight` rule packs (deterministic) |
| Plan authoring | Pi (fills the versioned plan schema / recipes) |
| Plan compilation + execution | `WorkflowRunCoordinator` → `TaskCenter` |
| Output verification | `harness:run_status` (automatic) |
| Map confirmation | `confirmMapOutput` (MapSpec preflight/repair) |
| Provenance | OutputCommitter / DerivationRecord / governance store |

## Subagents (Phase 17)

Roles are prompt contracts in `pi/roles/README.md` — data-inspector,
remote-sensing-planner, scientific-reviewer, cartography-reviewer,
result-verifier. They use the same tool surface; reviewers are read-only by
construction (their tool set is inspection/preflight/verification only), and
the **single-writer rule** keeps one mutating agent per project at a time.
ExpRS enforces safety in the tools themselves; Pi enforces discipline among
agents.

## Failure semantics

- Tool failure → MCP result with `isError:true` + `errorCode`/`errorCategory`/
  `retryable`; the adapter re-throws preserving them.
- Harness failures additionally carry the typed `HarnessError` envelope
  (`summary`, `details`, `recoverable`, `suggested_actions`, `retry_class`).
- Protocol faults are JSON-RPC errors; an `id:null` error fails all pending
  requests fast.
