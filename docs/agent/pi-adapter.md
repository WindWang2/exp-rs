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
   `meta,spatial,data,temporal,cartography,symbology,workflow,workspace,layout,harness,mission`)
   and registered as Pi tools named `exprs_<sanitized>`. The `scientific:`
   family (durable agent sessions: `scientific:agent_session` /
   `scientific:context` / `scientific:capabilities`) is a deliberate
   opt-in: add `scientific` to the env var to bridge it. A bridged tool
   whose sanitized name collides with an already-registered tool is
   rejected explicitly and reported through `exprs_status`
   (`details.nameCollisions`), never silently shadowed.
5. Tool calls dispatch over the bridge; results are truncated to the last
   50,000 chars; abort maps to `notifications/cancelled` → `TaskCenter::cancelTask`.
   Since R30-T05, dispatch is GATED on schema drift: after a server respawn,
   a call may only reach the generation whose catalog comparison landed,
   and a tool whose live schema drifted from the Pi registration is refused
   with a typed non-retryable `SCHEMA_DRIFT` (restart the session to
   re-register); an unsettled or failed comparison refuses retryable
   `SCHEMA_UNVERIFIED` (one comparison retry per call). A call that crossed
   a respawn boundary is re-verified — a response from an unverified,
   drifted generation is discarded with a warning that the call may already
   have executed server-side. Unchanged tools keep dispatching.
6. `exprs_status` is registered on EVERY path (healthy startup included):
   it reports live connectivity, server identity/protocol/generation,
   schema-level catalog drift after a respawn (`added`/`changed`/`removed`,
   plus `comparisonGeneration` — the generation whose comparison landed),
   rejected name collisions, unmet dependencies, and next actions — both as
   human text and machine-readable `details`.

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

## Result semantics (R29-T03, R30-T05)

- Successful tool results keep a bounded text projection (tail-preserving
  truncation at 50,000 chars) AND gain machine-readable `details`: the
  server's single text-JSON document is mirrored whole into `details` when
  it parses; a native MCP `structuredContent`, if a future server sends
  one, is mirrored with TOP-LEVEL strings capped at 4096 chars (the text
  projection stays the bounded surface).
- A truncated document does not parse, so `details` carries
  `resultTruncated`/`resultDroppedChars` plus a continuation that matches
  what can actually be recovered (R30-T05): when the tail holds a paged
  artifact envelope, `details.artifact` preserves `path`/`sha256`/
  `size_bytes`/cursor and `resultContinuation` names the exact
  `exprs_artifact_read` call (path, resume offset, a budget-fitting page
  length) — including how to re-read the page head the 50k cut dropped.
  Results with NO paged artifact honestly advise a narrower
  filter/pagination: the server elides outputs beyond its 512 KiB tool
  budget instead of spilling them to files, so a generic artifact_read
  hint would point at a file that does not exist.

## Waiting semantics (R30-T05)

`exprs_wait_for_execution` polls `get_execution_status` to a real terminal
state under a total `timeout_seconds` budget that bounds status requests
and sleeps alike. `timeout_seconds` must be finite and positive (refused
immediately otherwise). A timeout STOPS THE WAIT ONLY — it never cancels
the execution; aborting the tool call REQUESTS a server-side cancel (a
late terminal arriving after an abort is reported as an abort, never
resurrected as a success, and "cancel requested" is never reported as
"canceled" — the settled status is the server's fact). Typed status-poll
errors (`isError`/`errorCode`/`retryable`) surface immediately; malformed
or unknown statuses fail fast after three consecutive strikes instead of
polling blind; `cancelling` is a non-terminal state the wait polls through.

## Scientific sessions (R29-T03)

With `scientific` in `EXP_RS_TOOL_CATEGORIES`,
`exprs_scientific_agent_session` drives durable plan→preflight→execute→
verify sessions on the same production engine as the CLI `session` command
and the desktop Control Center. Long-run semantics: `run`/`resume` return
their full envelope synchronously when fast (typed refusals, dry runs); a
session still running after the host's bounded wait returns
`{ok:true, status:"running", session_id}` — poll `action:"status"` (live
running state, then the final envelope) and control with
`action:"cancel"`/`action:"pause"`, both answerable WHILE the session
executes (cancellation reaches the actual execution through the loop's
cancel-aware polling). One session at a time per host;
`SESSION_EXECUTOR_BUSY` names the running session. Session refs accept
explicit typed objects `{"path": …}` (workspace-anchored, scheme-checked)
and `{"asset_id": …}` (registry identity, never anchored) — plain strings
keep the legacy heuristic.
