# Integration seams — RS14 agent-benchmark ↔ platform

This track ships a self-contained pure-C++ harness. The points below are the
DESIGNED wiring surfaces for future tracks; none of them are required for the
harness to work, and none were built here to avoid cross-track coupling.

## 1. Live trace capture (agent run-loop / harness session)

`AgentTrace` (`sicnu.agentbench.trace/v1`) is the target projection for a
session recorder. A future track can serialize real agent sessions (run-loop
steps, SpatialToolResults, token counts) into traces. Injected faults must be
marked `payload.fault = <kind>` for `recovery_quality` to observe them;
without markers the metric is honestly `null`
(`faults_not_observable_in_trace`).

## 2. Persisting suite reports (ExperimentStore)

Suite/case reports are files, not a new store. The single-store discipline
stays intact: a Qt-side adapter can project a suite report into
`ExperimentStore::saveBenchmarkResult` (D19 tables, additive, schema stays
v1) keyed by `suite_id@version` + `pack_digest`. Nothing in `src/agentbench`
links Qt — the adapter belongs to the experiment track.

## 3. MCP surface

Extend the existing `benchmark:` namespace in `src/agent/data_platform_tools.cpp`
(e.g. `benchmark:agent_suite_run`, `benchmark:agent_report_read`) instead of
adding a new namespace. The harness's pure functions are directly callable
from that lane.

## 4. Teaching / lab surface

Per-case Markdown reports are designed for classroom review: verdict,
per-metric table (including *why* a metric is unavailable), hidden-invariant
outcomes, and resource usage. The lab track can render `renderReportMarkdown`
output next to lab reports (`sicnu.labreport.v1` stays the lab truth source).

## 5. Fake agent as oracle harness

`runScript` gives deterministic agent behavior for any surface that needs a
repeatable "agent" (determinism censuses, UI demos, grading examples). It is
NOT a planning engine — scripts are explicit policies, not intelligence.

## Boundary rules honored by this track

- No second registry, store, or provenance system.
- No metric-formula duplication (agent-behavior metrics only).
- No engine execution; no network; no new CI.
- Open issues in the avoid-list were not touched (see
  `.planning/RS14-14-agent-benchmark/recon.md` dedup matrix).
