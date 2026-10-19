# R18-C Review Round 1 (independent reviewer: fresh Explore context, authored none of the patches)

Scope: `git diff origin/master...HEAD` (9 commits at review time).

## Restore verification — PASS
- `src/agent_loop/session_seams.cpp`, `src/workflow/workflow_checkpoint.cpp`, `src/plugins/host/plugin_host_worker_main.cpp` — empty diff vs a8d5cd510 (verbatim restores).
- `pi/mcp_bridge.ts` vs a8d5cd510 — only the timeout-cancel + requestTimeoutMs additions.
- `src/agent/mcp_server.cpp` vs a8d5cd510 — only the intended R18-C deltas.

## Findings
| # | Sev | Summary | Disposition |
|---|---|---|---|
| 1 | P1 | surface-first get_tool_schema captured catalog tools too: category hardcoded "Processing", displayName lost (host-dependent vocabulary) | FIXED in 7598a5d80 (catalog hits keep the rich branch; only Meta/Data resolve via projection) |
| 2 | P2 | pi rpc-id reuse across bridge generations can cancel an unrelated live run (pre-existing; timeout path makes it routine) | KNOWN RISK in PR body; needs generation-scoped keys as follow-up (map entries are one-shot consumed; bounded 1024) |
| 3 | P2 | list_tools/search_tools still silently rewind bad cursors (same class as the tools/list fix) | FIXED in 7598a5d80 (mcpTypedCursor, 4 sites) + R2 follow-up (search_algorithms); data_platform pageCursor → CT-3 |
| 4 | P3 | pre-spec cancel fallback narrowed for `params.requestId: ""` + top-level id | ACCEPTED (theoretical; pre-spec clients send only the top-level form) |
| 5 | P3 | rpcCancelMapKey double collision ≥2^53 | RESOLVED in R2 follow-up ('g',17 formatting) |
| 6 | P3 | no wire-level test for the Double-keyed cancel path | ACCEPTED (both sides parse through the same JSON path; pi test pins the numeric id on the wire) |
| 7 | P3 | qBound clamp masks a hypothetical legacy 0..100 reporter as "complete" | ACCEPTED (no such caller exists; producers verified 0..1 at every seam) |
| 8 | P3 | RateLimiter swallows backward moves (pre-existing) | NOTED |
| 9 | P3 | parity sweep partially tautological (same projection both legs) | ACCEPTED — the independent gate is test_surface_parity's registry sweep; the sweep still pins "every listed name resolves" |
| 10–16 | P3 | timeout/notify race safe; StdinReader edges correct; workflow_definition/plugin fail-closed semantics intended; ghost log acceptable; normalization catch complete; cancel test has moderate CI flake risk (bounded waits) | NOTED / accepted |

## Verdict: READY (no P0) — with P1 finding 1 fixed before merge.
