# R14-C Goal

Harden the existing Pi↔exp-rs agent stack — MCP surface, tool catalog, task DAG/workflow runtime, IPC, plugin/worker lifecycle, session journal, recovery and error evidence — for consistency and determinism. **No new product directions, no second agent framework.** Pi stays the external agent runtime; exp-rs stays the spatial capability/workflow provider.

## Work packages (WP01–WP15)

Each WP: Observe → RED repro (or ALREADY_FIXED evidence) → minimal fix → GREEN×2 → independent review → ledger. Status vocabulary: `FIXED / ALREADY_FIXED / NOT_APPLICABLE / BLOCKED_WITH_EVIDENCE`.

01 Intent→tool-execution flow map (state machine + failure matrix from real classes/tests)
02 MCP schema contract (tools/list, tools/call, JSON-RPC envelope, typed errors, unknown methods, shutdown)
03 Pi adapter & disconnect recovery (`pi/exp-rs-spatial.ts`, mcp_bridge subprocess framing/timeouts/restart)
04 Tool catalog single authority (no duplicate/stale/unauthorized tools; listed ⇒ callable)
05 Plugin register/unload (name conflicts, owner boundaries, hot reload, crash, dangling pointers)
06 IPC framing/backpressure/replay (bounded queues, close edges, cancel-before-reply, retry idempotency, partial frames)
07 Python isolated worker lifecycle (init failure, restart, pool shrink, QT cross-thread)
08 Workflow DAG & IR contract (workflow_ir_v2, token/param mapping, fallback, cycle detection)
09 Task run & cancel (exactly-once submit, cancel/complete race, terminal fanout)
10 Checkpoint & crash recovery (bounded reader, torn writes, cross-process lock release, resume state machine)
11 Agent loop journal (eviction keeps goal-normalization + decision evidence)
12 Security execution boundary (external command normalization, cwd/PATH, quotas, typed denial)
13 Planner/repair selection determinism (no second planning authority; stable ordering)
14 Spatial task end-to-end parity (GUI/CLI/Pi share core execution chain)
15 Runtime service performance (list tools, catalog reload, batch validation, run queue latency — measured before/after)

## Non-goals

- No rewrite of startWorker/IPC/replay already fixed by #1480/#1484/#1477.
- No edits into D/E/B/A ownership; no CI waits; no auto-merge.
