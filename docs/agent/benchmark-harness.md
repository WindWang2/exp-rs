# Agent Benchmark & Evaluation Harness (RS14)

Offline, evidence-driven benchmarking for remote-sensing agents: goal-level
cases, whole-trajectory grading, deterministic replay, fault injection, and
version-pinned reports. Implemented in `src/agentbench` — pure C++20 +
jsoncpp, no Qt, no GDAL, no engine. Evaluation is **traces-in**: the harness
never executes tools, never opens a store, never schedules work.

## What it grades (and what already existed)

| Layer | Surface | Question answered |
|---|---|---|
| Tier A (existing) | `data/agent/evals/` + corpus runner | Does *this tool call* return the contractually correct response? |
| Tier B (existing) | engine suites, experiment store | Does *this workflow run* execute and record truthfully? |
| **Tier C (this harness)** | `data/agent/bench/` + `src/agentbench` | How well does *this whole trajectory* achieve the scientific goal within budget — and can that verdict be reproduced? |

## Wire schemas (all document-versioned; readers refuse foreign versions)

- `sicnu.agentbench.case/v1` — goal-level case: goal, initial state
  (`workspace_roots` scope), allowed tools, hidden invariants, resource
  budget, expected evidence, optional fault schedule + failure expectation.
- `sicnu.agentbench.script/v1` — deterministic fake-agent policy; per-step
  failure discipline `abort | retry_once | skip`; honest outcome claims.
- `sicnu.agentbench.trace/v1` — trajectory: ordered tool calls with recorded
  results (injected faults carry a `payload.fault` marker), final evidence,
  explanation, outcome claim, stop reason.
- `sicnu.agentbench.evaluation/v1` — per-case report: verdict, failure class,
  8 metric results, invariant results, resource usage, digest.
- `sicnu.agentbench.suite_report/v1` — pack report: per-entry verdicts +
  `pack_digest` over sorted (case, source) digest pins.

## Metrics (null-with-reason when not computable — never zero-filled)

`task_completion`, `scientific_validity`, `unnecessary_transformations`,
`plan_efficiency`, `verifier_pass_rate`, `recovery_quality`,
`reproducibility` (double-evaluation digest oracle), `explanation_completeness`.
Exact formulas live in `src/agentbench/evaluator.h`; they never re-derive the
classification/regression formulas of `src/experiment/evaluation.*`.

## Failure taxonomy (closed)

`none, not_started, incomplete, scope_violation, budget_exhausted,
invalid_science, verification_failed, silent_failure, recovery_failed,
claim_mismatch, impossible_task` — classification priority documented in
`evaluator.h`; deterministic by construction.

## Verdicts

`PASS | PASS_WITH_WARNINGS | FAIL` (wire strings aligned with the platform
harness tri-state). A missing deliverable, a scope violation, a mispaired
case/trace, or any error-severity invariant failure is a FAIL — silent
fallback is impossible by construction. `claim_mismatch` flags a completed
passing run the agent itself denies; `impossible_task` is reserved for the
live-capture seam. Case-declared `failure_expectation` is advisory and
surfaced in every evaluation document.

## Usage

```cpp
#include "agentbench/case_schema.h"
#include "agentbench/fake_agent.h"
#include "agentbench/evaluator.h"
#include "agentbench/suite.h"

const auto agentCase = parseCase(caseText).parsed.value();
const auto script    = parseScript(scriptText).parsed.value();
const auto trace     = runScript(agentCase, script).trace.value();
const CaseEvaluation evaluation = evaluateCase(agentCase, trace);   // pure
const auto suiteRun  = runSuite(parseSuite(suiteText).parsed.value(), loader); // loader-injected
```

Tests: `ctest -R "^test_agentbench" -j1` (pure lane, seconds, offline).

## Versioning policy

- Framework version: `sicnu::agentbench::kAgentBenchFrameworkVersion`.
- Schema tags are per-document; changing semantics = new `/vN` + reader gate.
- The starter pack pins its digests in `tests/test_agentbench_corpus.cpp`;
  content changes require a conscious `suite.json` version bump in the same
  commit. Historical scores stay explainable via (framework version, suite
  version, pack digest).

## Non-goals

No live model calls, no new store (projections into `ExperimentStore` are the
documented future seam, see `docs/integration.md`), no GUI, no CI
dependencies.
