# Plan — RS14-14-agent-benchmark

## Problem statement

The platform can grade single tool calls (Tier A corpus) and engine workflow
runs (Tier B suites), but cannot answer: *"how well does an autonomous agent
perform a whole scientific remote-sensing task?"* There is no goal-level case
schema, no offline scorer for whole trajectories, no deterministic fake agent,
no recorded-trace replay, no versioned benchmark report. Scores across
harness versions are not comparable, and failures are not classified.

## User stories

- **Undergraduate (teaching view):** after an agent-assisted lab exercise, the
  student opens a human-readable benchmark report showing *what the agent
  actually did* — wasted steps, skipped verification, unsupported claims —
  with the same metrics their instructor sees, making the scientific process
  feedback concrete instead of a binary "the button worked".
- **AI agent (machine view):** a future agent (or its harness) consumes
  `sicnu.agentbench.report/v1` JSON: typed metric results, closed failure
  taxonomy, per-invariant outcomes — all machine-readable, offline,
  version-pinned — to self-assess and regress-test behavior across versions.
- **Benchmark maintainer:** adds a case as a versioned JSON document; a
  corpus-validation test keeps the pack self-consistent; historical scores stay
  explainable because every case, trace, and report carries a schema version
  and content digest.

## Architecture

```
data/agent/bench/                     corpus (data, versioned)
  suite.json                          sicnu.agentbench.suite/v1 — pins cases
  cases/*.json                        sicnu.agentbench.case/v1
  traces/*.json                       sicnu.agentbench.trace/v1 (recorded examples)

src/agentbench/                       pure C++20 + jsoncpp, no Qt (shipped)
  case_schema.{h,cpp}                 AgentCase parse/validate/version/digest
  trace.{h,cpp}                       AgentTrace parse/validate/scope/budget accounting
  fake_agent.{h,cpp}                  deterministic scripted agent → trace
                                      (fault application + failure policy live here)
  invariants.{h,cpp}                  hidden-invariant checker (closed kinds)
  failure_taxonomy.{h,cpp}            closed outcome-failure classification
  evaluator.{h,cpp}                   case + trace → CaseEvaluation (verdict, 8 metrics,
                                      invariants, taxonomy; recovery scoring lives here)
  report.{h,cpp}                      evaluation JSON + Markdown writers
  suite.{h,cpp}                       suite runner: N cases × (script|trace) → suite report
  json_writer.{h,cpp}                 local deterministic serializer (to_chars, locale-free)
  json_numbers.h                      shared dotted-path + int-range helpers
  errors.h / version.h                typed agentbench.* codes; framework version

tests/test_agentbench_*.cpp           Catch2, TEST_PREFIX "test_agentbench_…::"
```

Evaluation is **traces-in**: never executes tools, never touches a store,
never opens the engine. Verdict wire strings align with the harness tri-state
(`PASS` / `PASS_WITH_WARNINGS` / `FAIL`). Unavailable metrics are `null` with a
machine-readable reason — never zero-filled.

## Public API / data schema (core types)

- `AgentCase` — `case_id`, `schemaVersion`, `title`, `taskFamily` (closed:
  `optical|classification|change|temporal|model|map_delivery`), `goal`,
  `initialState` (declared assets/state JSON), `allowedTools` (closed list),
  `hiddenInvariants[] {id, dimension: scientific|process, severity:
  error|warning, kind, params}`, `resourceBudget {maxToolCalls, maxTokens,
  maxRetries}`, `expectedEvidence[] {id, kind, requiredFields[], path?}`,
  `minimalSteps` (reference plan length), `faults[]` (optional), `redundantTools[]`
  (optional, declared wasteful-for-this-goal tools), `failureExpectation`
  (optional, for impossible/decoy cases), digest.
- `AgentTrace` — `trace_id`, `caseId`, `agent {name, kind: fake|recorded,
  version}`, `seed`, `steps[] {index, tool, input, result {success, errorCode?,
  payload}, tokens}`, `finalEvidence[]`, `explanation`, `outcomeClaim
  {success: bool, note}`, `stopReason: completed|budget_exhausted|gave_up|blocked`.
- `InvariantResult {invariantId, passed, severity, evidence}`; `MetricResult
  {name, value|null, reason?, notes}`; `CaseEvaluation {verdict, metrics[8],
  invariants[], failureClass, resourceUsage, digest}`; `SuiteReport`.
- Error codes (`agentbench.*`): `agentbench.schema_version_unknown`,
  `agentbench.case_malformed`, `agentbench.case_invalid`,
  `agentbench.trace_malformed`, `agentbench.trace_invalid`,
  `agentbench.script_invalid`, `agentbench.tool_not_allowed`,
  `agentbench.path_outside_scope`, `agentbench.budget_exceeded`,
  `agentbench.suite_invalid`, `agentbench.report_write_failed`.
  (Invariant-kind and fault-kind unknowns fold into `case_invalid` /
  `script_invalid` at parse time with field-level details.)

## Closed invariant kinds (Slice D, extensible via version bump)

`tool_used`, `tool_not_used`, `step_order`, `result_success`,
`error_code_present`, `evidence_exists`, `field_equals`, `field_contains`,
`numeric_le`, `numeric_ge`, `verdict_is`, `explanation_mentions`,
`claim_consistent`, `budget_within`.

## Metrics (all null-with-reason when not computable)

1. `task_completion` — error-severity invariants passed / total.
2. `scientific_validity` — scientific-dimension invariants passed / total.
3. `unnecessary_transformations` — count of redundant steps (repeated
   identical successful (tool,input) pair, or `redundantTools` hits).
4. `plan_efficiency` — `minimalSteps / stepsUsed`, clamped to [0,1].
5. `verifier_pass_rate` — expected-evidence entries satisfied / total.
6. `recovery_quality` — handled / observable injected faults. Observable =
   the trace carries the fault marker (`payload.fault`); handled = some later
   step succeeded (a retried call or any subsequent progress). Null when the
   case injects no faults or marks are unobservable.
7. `reproducibility` — 1.0 iff double evaluation of the same inputs yields
   identical canonical documents, else 0.0 (harness-side determinism canary;
   unfailable for a correct evaluator by construction).
8. `explanation_completeness` — non-empty explanation + evidence explicitly
   required in the explanation (its id must appear) + honest outcome claim,
   over that requirement total.

## Failure taxonomy (closed, classified by evaluator)

`none`, `not_started`, `incomplete`, `scope_violation` (including mispaired
case/trace), `budget_exhausted`, `invalid_science`, `verification_failed`,
`silent_failure`, `recovery_failed`, `claim_mismatch` (a completed passing run
the agent denies), `impossible_task` (reserved for the live-capture seam).
Priority order documented in `evaluator.h`; `failure_expectation` in a case
is advisory and surfaced in the evaluation document, not enforced.

## Migration / compatibility

Purely additive. No existing file gains new responsibilities; no schema of
another module changes. Removal path: delete `src/agentbench/`,
`data/agent/bench/`, the test blocks, and the two CMake lines (kill-switch).

## Observability

Every evaluation emits the report JSON with per-metric reasons, per-invariant
evidence, resource usage vs budget, case/suite digests and schema versions —
inspectable without re-running anything. Human Markdown report mirrors it.

## Security / trust boundary

Readers parse repo-local trusted fixtures with a depth limit set on jsoncpp
readers (structural defense; the corpus is data, not attack surface). Reports
write only to caller-supplied output directories; tests use temp dirs. No
network, no subprocess, no path escaping the declared scope (traces carrying
paths outside the case scope are typed errors, not silently graded).

## Performance budget

Evaluation is O(steps × invariants + evidence) per case — microseconds at
starter-pack scale. Corpus bound: `runSuite` refuses suites with more than
400 entries (mirrors the Tier A corpus bound). Tests run in seconds; no
engine, no GDAL.

## Test strategy

Catch2, lane: plain `add_executable` + `Catch2::Catch2WithMain` +
`sicnu_agentbench`, `TEST_PREFIX` for narrow `ctest -R "^test_agentbench"`.
Every slice RED-first. Required coverage per behavior: happy path, invalid
input, boundary, serialization round-trip, deterministic replay, budget/scope.
Adversarial pass must demonstrate each metric can *fail* (anti-vacuous oracle).

## Work packages = TDD slices (see slices.md)

0 planning + skeleton → A case schema → B trace/replay/accounting → C fake
agent → D invariants → E metrics+taxonomy → F faults → G report → H pack +
suite + pin. Two review gates before PR.

## Rollback / kill-switch

Single `ENABLE_AGENTBENCH`-free design: module is additive; deleting the
subdirectory + 2 CMake edits removes it entirely. No option needed (tests
already gated by `ENABLE_TESTS`).

## Definition of Done

1. `ctest -R "^test_agentbench" -j1` fully green locally.
2. ≥ 20 cases in the pack, corpus-validation test enforces schema + coverage
   families + uniqueness + budget sanity.
3. Byte-deterministic double-run test (report digest equality) passes.
4. Recorded-trace replay + fake-agent paths both exercised by tests.
5. Fault-injected cases demonstrate recovery metric ≠ null and can fail.
6. Two review passes recorded; P0–P2 fixed; targeted regression green.
7. `docs/agent/benchmark-harness.md` + `docs/integration.md` written.
8. PR with goals/architecture/files/tests/dedup/boundaries.
