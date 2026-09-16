# Agent Surface Contracts — discovery, progress, cancel, artifacts (Surface-11)

> Scope: the machine-facing contracts shared by the MCP server (`sicnu_geo_rs
> --mcp`), the CLI (`sicnu_geo_rs_cli tools|batch`), and the Pi bridge
> (`pi/exp-rs-spatial.ts`). Established by the cli-mcp-agent-surface-11 track;
> normative for anyone touching tool registration, schemas, or protocol
> behavior.

## 1. The union projection is THE discovery surface

`agent/tool_catalog/surface_registry.h` — `collectSurfaceTools()` is the one
function that enumerates every agent-callable tool, in deterministic wire
order:

1. **meta protocol tools** (`tool_catalog/meta_protocol_tools.cpp` table:
   `list_algorithms` … `resume_workflow`, `artifact_read`);
2. **data-platform tools** (`data_platform_tools.h` table: `dataset:` /
   `experiment:` / `reproducibility:` / `benchmark:` namespaces);
3. **catalog tools** (AgentToolCatalog: processing, interaction, data,
   spatial providers) filtered by the allow-prefix policy
   (`surfaceIdAllowed`) and the headless-GUI rule (GUI-only interaction
   entries are hidden when the process has no interaction registry).

**Contract:** MCP `tools/list`, the CLI `tools list|search|schema` command,
and the `get_tool_schema` fallback all render this projection. A tool that is
dispatchable but not projected — or projected but not dispatchable — is a bug;
`tests/test_surface_parity` gates both directions.

**Schema authority:** each projection entry carries the input schema from its
own source of truth (meta/data-platform tables, catalog descriptors). There
is no second schema database; `data/processing/algorithm_meta/*.json` sidecars
remain resolved-against-descriptor (#707).

**Adding a tool:** register it where its family lives (spatial registry,
algorithm registry, a table) — projection/discovery pick it up automatically.
If you add a namespace family, extend `surfaceIdAllowed` (listing policy) AND
the `tools/call` routing in `mcp_server.cpp` together; the parity gate fails
if either is forgotten.

## 2. Tool results are bounded; big data lives in files

Hard caps (unchanged from master where noted):

| Boundary | Cap |
| --- | --- |
| MCP stdio line (input) | 4 MiB (`kMaxMcpLine`) |
| Spatial tool result envelope | 512 KiB with compaction |
| MCP list endpoints | page clamp 1..500 |
| `artifact_read` slice | 256 KiB raw per call (`kMaxArtifactChunk`) |

**Contract:** a tool result never inlines raw data arrays. Results that
reference large outputs return file paths (workspace-resolvable) or committed
asset ids; agents retrieve slices with `artifact_read {path, offset, length,
encoding}`. Every response carries the whole-file `sha256` (a cursor walk can
detect concurrent modification), `size_bytes`, `truncated`, and `next_offset`
when more data remains. `encoding:"text"` rejects non-UTF-8 slices instead of
corrupting them — re-read with `encoding:"base64"`.

## 3. Progress: one source, two projections

TaskCenter is the execution authority (status, progress, cancel, logs). The
surface projection is read-only:

- **MCP:** a `tools/call` request carrying `params._meta.progressToken`
  (MCP 2024-11-05) is subscribed for the lifetime of the submitted task. The
  server emits `notifications/progress {progressToken, progress, total}`
  with `progress ∈ [0,1]`, rate-limited to one event per 5-point progress
  step plus a state change; **exactly one terminal event** is emitted per
  task (subscription is removed on the terminal transition). Clients without
  a progressToken see zero notifications (behavior identical to pre-Surface-11).
- **CLI:** long-running commands and `batch run` emit NDJSON progress records
  on **stderr** (`{"type":"progress","step":N,"steps":M,"percent":P,
  "message":"…"}`) when `--progress-json` (or any `--json*` flag) is set;
  stdout stays clean for the result envelope. `--quiet` suppresses.
- **Pi:** unchanged polling flow (`exprs_wait_for_execution`) still works;
  bridges may adopt the progressToken instead.

**Terminal-state uniqueness:** a task reaches exactly one terminal status
(`completed` / `failed` / `canceled` in the ADR 0022 vocabulary) and never
leaves it; repeated `get_execution_status` polls after the transition answer
the same terminal label. Cancellation via `notifications/cancelled
{requestId}` is idempotent and one-shot (the rpc-id→task map entry is
consumed).

## 4. Batch manifests (CLI)

`sicnu_geo_rs_cli batch run <manifest.json|.jsonl> [--fail-fast] [--dry-run]
[--result-index <path>] [--var k=v ...]`

- **Object form:** `{version:1, variables:{…}, policy:{on_error:"continue"|
  "fail-fast"}, tasks:[{id, operator, params?, params_file?, enabled?}]}`.
- **JSONL form:** one task object per line; `#` comments and blank lines are
  skipped; an optional first line carrying `variables`/`policy` (no
  `id`/`operator`) is a header.
- **Variables:** `${name}` interpolation inside any string in `params`;
  manifest `variables` overridden by `--var`. Unknown references fail the
  task with exit code 6 (InvalidInput).
- **Policy:** default `continue`; `--fail-fast` overrides. Tail tasks after a
  stop are recorded as `skipped` with the reason.
- **Cancellation:** SIGINT marks the running task `cancelled` (exit 4), all
  remaining tasks `skipped`; the result index is still written.
- **Result index:** NDJSON, one record per task
  (`{index,id,operator,status,exit_code,error?,duration_ms}`), written
  atomically (tmp + rename).
- **Exit codes (exprs::ExitCode):** 0 all ok/skipped; 4 any cancellation;
  otherwise the maximum task exit code (2 validation, 3 execution failure,
  5 unknown operator, 6 invalid input). A structurally invalid manifest is 2
  (validate) / 6 (unreadable or bad JSON at run).
- Unknown keys anywhere in the manifest are contract violations (strict
  parsing) — a typo'd `on_error` must not silently keep the default.

## 5. Protocol hygiene (MCP)

- Framing: line-delimited JSON (newline-terminated, compact); an oversized
  line is answered with `-32700` ("line too long") and the connection stays
  usable for the next line.
- Pre-initialize requests (except `initialize`/`ping`/notifications) get
  `-32002`.
- Unknown method → `-32601`; unknown tool inside `tools/call` → `-32602`
  (per #620).
- Execution failures are RESULT objects with `isError:true` plus
  `errorCode`/`errorCategory`/`retryable` — never JSON-RPC errors (#620).
- `initialize` responds with the version this build supports
  (`2024-11-05`), per spec — clients are expected to disconnect when they
  cannot handle it. `resources/list`/`prompts/list` are honest empty lists.
- The server processes requests sequentially on the main loop; clients that
  need concurrency run multiple sessions (pi/mcp_bridge.ts manages one
  child per session with fast-crash circuit breaking).

## 6. Redaction at the protocol boundary

Tool **error messages** (and CLI batch result-index `error` fields) pass
through `tool_catalog/surface_redaction.h`: bearer/JWT tokens, Authorization
headers, PEM private-key blocks, sensitive-name assignments
(`api_key=`/`password:`/`secret:`/`token=`…), and `scheme://user:password@`
URLs are replaced with redaction markers. Over-redaction is tolerated;
leaking is not. **File paths are NOT redacted** — they are the business data
of this product; exposure is governed by the `SICNU_MCP_WORKSPACE` sandbox,
which `artifact_read` enforces like every other path-taking tool.

## 7. Pi surface

`pi/exp-rs-spatial.ts` consumes `tools/list {includeSchemas:true}` — it owns
no schemas. Its category filter (`EXP_RS_TOOL_CATEGORIES`) must stay a subset
of the projection's families; `tests/test_surface_parity` checks the default
list statically against the live projection. The transport
(`pi/mcp_bridge.ts`) is the reference client for framing, timeouts
(10 min/request), crash circuit-breaking, and abort →
`notifications/cancelled`.
