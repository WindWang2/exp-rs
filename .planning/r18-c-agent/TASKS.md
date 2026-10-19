# R18-C staged work packages

Each stage: Recon → RED reproduction → root cause → minimal fix → GREEN ×2 → stability/perf comparison → independent review at gate. One attributable change per iteration, logged in LOOP_LEDGER.md.

## C01 Protocol authority & mapping — status: planned
Single-source-of-truth checks from Pi layer to C++ MCP output: tool name/category mapping, manifest, input/output schema, error codes, capability discovery. Schema snapshot + drift assertions; no second table.
Candidates (verify RED first): `inputSchema` vs `schema` key drift across `tools/list` / `get_tool_schema` / `list_tools` / `search_tools`; compact `tools/list` entries omitting required `inputSchema`; `get_interaction_schema` `tool_id` alias undeclared in schema; `list_tools`/`search_tools` limit not clamped to 500 (drift from `mcpPageLimit`); silent cursor→0 rewind in `tools/list`; Pi-side silent empty-schema fallback; Pi category-case asymmetry.

## C02 Connection & lifecycle — status: planned
Deterministic fault injection via fake MCP server (extend `pi/test/fake_mcp_server.mjs`): request-timeout path (no server cancel today), hung-initialize zombie state, stop-during-startup orphaned pending initialize, spawn ENOENT, circuit-breaker threshold boundary, `id:null` −32700 fan-out, protocol-version mismatch. Verify which r15-D already fixed (failed-start generation discard) before acting.

## C03 Tool real availability — status: planned
Model-present ≠ callable: offline state, permissions, input-asset readiness, capability/defaults; machine-readable refusals (no fake success). Candidates: `handleAgentSession` SEAMS_UNAVAILABLE envelope with `isError:false` (second-level nesting drift); `exprs_status` absent on healthy sessions (no liveness probe); progress unit mismatch 0..1 vs 0..100 in `onTaskUpdated`/RateLimiter (test-masked); unredacted client text in protocol errors.

## C04 Workflow structure & recovery — status: planned
DAG validation, port binding, placeholders, cache keys, cycles, versioning, checkpoint, restart-resume, idempotency, terminal states, hash/provenance non-mixing. Candidates: `Cancelling→Completed` illegal-but-forced ×3 sites (centralize `goTerminal` or legalize edge); premature step `Cancelling→Canceled` fold → verdict flap; ghost-checkpoint unlink failure unlogged; default id `"agent_plan"` collision; `findStepPlan` lock escape.

## C05 Cancel / timeout / backpressure — status: planned
Request queue bounds, cross-process timeouts, Task Center cancel, manual agent kill, multi-client sessions; per-level propagation; no deadlock, no fake completion, no eternal pending. Candidates: Pi request timeout without `notifications/cancelled`; wait-tool fire-and-forget `cancel_execution` respawn side effect; hard-coded 30-min plane await (make policy-driven or documented); cancel-map id collision (numeric 5 vs string "5").

## C06 Science context & permissions — status: planned
ScienceContext authority lifecycle, autonomy policy, interactive approval, safe refusal, tool choice/repair suggestions; blocked must not be bypassed by planner repair; no unexplained permission drift across turns. Candidates: broker cache digest gap for live-asset content vs comment claims; L5-only autonomous exec open-question flow.

## C07 Diagnostics, repair, rerun — status: planned
Typed errors → recovery suggestion → param fix/model switch/human interaction (existing UI/API only) → rerun; security boundaries never auto-"repaired"; failure paths keep evidence + user decision. Avoid sibling track's `repair_schema.*`.

## C08 Session bookkeeping & observability — status: planned
Journal atomic writes/reads, state closure, cancel/fail/restart terminal states, context summary merging; assertable event traces. Candidates: journal compaction can exceed 1 MiB contract (payloads bounded, decisions not); ignored `note()`/`append()` return values; `internal_error` drop path.

## C09 Fault matrix & adversarial harness — status: planned
Bad JSON, unauthorized tools, malformed schemas, duplicate requests, process exit, reconnect, off-spec outputs, induced wrong fallbacks — real RED→GREEN per surviving boundary. No real secrets/PII. Deliverable: `tests/test_r18_c_fault_matrix*.cpp` + `pi/test/r18_c_*.test.mjs`.

## C10 End-to-end golden path — status: planned
Existing remote-sensing operators + synthetic rasters: Pi → MCP discovery → plan → run workflow → status → verify → archive. Only C-domain interfaces; D/E/A/B root causes go to CROSS_TRACK_FINDINGS.md. Includes the `TaskCenterExecutor` MULTI_STEP_PLAN_UNSUPPORTED gap decision (execute via workflow coordinator seam or keep fail-closed with a machine-readable reroute suggestion).
