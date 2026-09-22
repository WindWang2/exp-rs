# Recon — RS14-14-agent-benchmark

Baseline: origin/master `4f6632e1f6` (PR #1145 merged). Dynamic dedup on 2026-09-21:
no open PRs; open issues all inside the track's avoid-list (none touch benchmark
harnessing). Parallel rs14 worktrees: `agent/rs14-explainable-workflow`,
`agent/rs14-scientific-inspector-ui` (same base commit, disjoint scope).

## Already exists (MUST reuse / project — never duplicate)

| Surface | Where | Role |
|---|---|---|
| ExperimentStore (schema v1) | `src/experiment/experiment_store.*` | the only store for experiments/runs/metrics/`benchmark_definitions`/`benchmark_results` |
| D19 benchmark stack | `src/experiment/benchmark_{definition,runner,compare,service}.*` | versioned, digest-pinned, predictions-in pipeline; "never schedules a second engine" |
| Metric formulas | `src/experiment/evaluation.*` | classification/regression/detection metrics, "live exactly once", `kMetricsSchemaVersion = 1` |
| Agent eval corpus (Tier A) | `data/agent/evals/` + `tests/test_harness_eval_corpus.cpp` | tool-contract-level grading: steps of tool calls against the live harness surface, asserts on dotted paths |
| Engine-level scenarios (Tier B) | `tests/test_harness_evals.cpp`, `test_harness_lab_evals.cpp` | full workflow-engine execution scenarios |
| Harness verdicts / errors | `src/agent/harness/harness_{verification,error}.h` | tri-state `PASS/PASS_WITH_WARNINGS/FAIL`, SCREAMING_SNAKE tool-error taxonomy — compiled into heavy SHARED `sicnu_agent`, not linkable from pure C++ |
| Perf observatory | `tests/perf/perf_observatory.h` | structural performance gates, unavailable-metric = null-with-reason |
| Identity stack | `runConfigHash` / execution fingerprint / `canonicalizeJsonRfc8785` (Qt lane, `src/data/execution_fingerprint.h`) | three-hash run identity; NOT reachable from pure C++ |
| Planning/docs conventions | `.planning/<track>/`, `docs/adr/NNNN-*.md` (next free ≥ 0172), topic docs | repo conventions this track follows |
| Pure-C++ library lane | `src/contracts/CMakeLists.txt` (C++20 + jsoncpp, no Qt) | CMake template for a new lightweight module |
| Light test lane | `tests/CMakeLists.txt`: `sicnu_add_sdk_test` (Catch2 + `sicnu_sdk` only), `TEST_PREFIX` naming | fast ctest-registered tests, no Qt/QGIS |

## The actual gap (what RS14 adds)

Existing evaluation grades **individual tool calls** (Tier A) and **engine runs**
(Tier B). Nothing grades a whole **agent trajectory** — goal → plan → tool
calls → evidence → explanation — against hidden task invariants, resource
budgets, and recovery expectations. Nothing produces a persisted,
versioned, machine-readable **agent benchmark report**, and there is no
deterministic fake agent or recorded-trace replay to score agent behavior
offline.

## Architecture position (decision)

New module `src/agentbench/` — namespace `sicnu::agentbench`, pure C++20 +
jsoncpp, **no Qt** (usable headless; fast tests; mirrors the `src/contracts`
lane). Evaluation is **traces-in**: it consumes an `AgentTrace` (recorded from
any surface, or produced by the deterministic fake agent) plus a versioned
`AgentCase`, and scores it. It never executes tools, never schedules work
(no second scheduler), never opens the experiment DB (no third store).

Wire schemas (document-level version tags, per repo pattern 3):
- `sicnu.agentbench.case/v1` — benchmark case: goal, initial state, allowed
  tools, hidden invariants, resource budget, expected evidence, fault schedule,
  failure-taxonomy expectations, minimal reference plan length.
- `sicnu.agentbench.trace/v1` — agent trajectory: steps `{tool, input, result,
  tokens}`, final evidence, explanation, outcome claim, stop reason, seed.
- `sicnu.agentbench.report/v1` — per-case evaluation + suite report: 8 metrics
  (task completion, scientific validity, unnecessary transformations, plan
  efficiency, verifier pass rate, recovery quality, reproducibility,
  explanation completeness), closed failure taxonomy, resource accounting.
- `sicnu.agentbench.suite/v1` — suite doc pinning case ids + pack digest.

Vocabulary alignment: outcome verdicts reuse the harness wire strings
`PASS` / `PASS_WITH_WARNINGS` / `FAIL` (vocabulary alignment, local enum — the
harness header itself is not linkable from this lane). Agentbench's own API
errors use `sicnu::data`-style snake_case typed codes with an `agentbench.`
prefix. Tool-level error codes inside traces are opaque data; agentbench never
re-interprets them (no second error taxonomy).

Digesting: a local deterministic JSON serializer (sorted keys, stable number
formatting) computes **report/replay digests only**. RFC 8785 identity
fingerprinting remains owned by the Qt-side experiment stack; agentbench digests
are never persisted as run identity. Documented in `docs/integration.md`.

## Interfaces to the other 19 tracks

- Data provided as minimal DTOs (`AgentCase`, `AgentTrace` value objects).
- Future wiring points (documented in `docs/integration.md`, not built here):
  1. live trace capture from the harness run-loop / session recorder →
     `AgentTrace` projection;
  2. suite report → `ExperimentStore::saveBenchmarkResult` via a Qt-side
     adapter (keeps single-store discipline);
  3. MCP surface: extend the existing `benchmark:` namespace, no new namespace;
  4. lab/teaching surface may consume per-case human reports.

## Risks

- **Second-truth-source drift** — mitigated by traces-in design, no store, no
  metric-formula overlap (agent-behavior metrics ≠ evaluation.h formulas).
- **Scope creep into open issues** — eval corpus / capability mirror /
  experiment-store defects are adjacent; we do not touch them.
- **20 parallel tracks** — module is additive: one new `src/agentbench/` dir,
  one `add_subdirectory` line, appended test blocks, `data/agent/bench/` corpus.
  No edits to central registries.

## Dedup matrix vs current open issues

| Issue | Overlap? | Disposition |
|---|---|---|
| #1151/#1187 capability mirror / contract projection red | adjacent (agent surface) | untouched; benchmark does not read the mirror |
| #1184/#1181 agent paging / tool catalog cache | agent runtime defects | untouched; traces are input data, catalog not used |
| #1154/#1155 jsoncpp depth bombs | security | we parse only repo-local trusted case/trace fixtures; depth guard set on our own readers |
| #1173 benchmark_results ↔ pruned runs | experiment store | untouched; our reports are files; store projection is future adapter work |
| #1177 perf baseline deleted | perf observatory | untouched; distinct from trajectory benchmark |
| #1179 test oracle potency | test quality | our own tests must NOT repeat this failure: every metric test has a failing-side case |

## Not doing

- No live model calls, no network, no new CI, no engine execution.
- No changes to `data/agent/evals/` (Tier A corpus stays as-is).
- No persistence into ExperimentStore in this track (documented seam only).
- No fixes to issues in the avoid-list.
