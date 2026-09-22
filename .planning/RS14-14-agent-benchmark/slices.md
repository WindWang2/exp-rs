# Slices — RS14-14-agent-benchmark

Each slice: RED (failing test for missing capability) → GREEN (minimal) →
REFACTOR → narrow ctest → adversarial oracle check → commit → progress.md.
Mapping to the campaign's recommended slices: A↔A (schema), B↔B+E (runner
lifecycle/sandbox + resource accounting), C+D↔C (metrics), B/C replay ↔D
(trace replay), F↔fault-injection, H↔F+G (20-case pack, self-consistency,
version pin).

## Slice 0 — planning + skeleton
- `.planning/` docs (this set); `src/agentbench/` CMake skeleton (empty lib +
  alias) + `tests/test_agentbench_core.cpp` smoke test proving the light lane
  links and `ctest -R "^test_agentbench"` discovers it.

## Slice A — case schema
- RED: minimal valid case parses; unknown `schema` version → typed
  `agentbench.schema_version_unknown`; missing goal / empty allowedTools /
  unknown taskFamily / unknown invariant kind / negative budget →
  `agentbench.case_invalid` with field-level details; digest stable across
  key order; round-trip toJson/fromJson equality.
- GREEN: `case_schema.{h,cpp}`.

## Slice B — trace schema + replay validation + resource accounting
- RED: valid trace parses; step index gaps → invalid; tool outside case
  `allowedTools` → `agentbench.tool_not_allowed` during replay validation;
  path outside declared scope → `agentbench.path_outside_scope`; over-budget
  steps flagged `agentbench.budget_exceeded` (usage vs budget accounted);
  recorded trace loads from JSON file; double replay → identical accounting.
- GREEN: `trace.{h,cpp}`.

## Slice C — deterministic fake agent
- RED: scripted policy + case → trace; two runs byte-identical (with fixed
  seed); script referencing a disallowed tool → typed script error; script
  with unfulfillable step → agent stops with `gave_up`/`blocked` (never fake
  success); stop-reason discipline (budget cut → `budget_exhausted`).
- GREEN: `fake_agent.{h,cpp}`.

## Slice D — invariant checker
- RED: every closed kind has happy + failing + invalid-params test;
  warning-severity does not flip verdict but is reported; unknown kind at case
  parse time already rejected (A); evidence surfaced per invariant.
- GREEN: `invariants.{h,cpp}`.

## Slice E — metrics evaluator + failure taxonomy
- RED: each of the 8 metrics has a passing case AND a demonstrably failing
  case (anti-vacuous); null-with-reason for recovery_quality on fault-free
  case; classification table: each taxonomy class has a triggering fixture;
  verdict aggregation rules (any error-severity fail ⇒ FAIL; only warnings ⇒
  PASS_WITH_WARNINGS).
- GREEN: `metrics.{h,cpp}`, `failure_taxonomy.{h,cpp}`, `evaluator.{h,cpp}`.

## Slice F — fault injection + recovery quality
- RED: fault schedule applies typed failures at scripted step boundaries;
  recovery (retry transient / route around unavailable) recognized; silent
  success over fault ⇒ `silent_failure` + recovery_quality penalized; invalid
  fault (unknown kind, bad anchor) → `agentbench.fault_invalid`.
- GREEN: `faults.{h,cpp}` wired into fake agent + evaluator.

## Slice G — report
- RED: report JSON carries `sicnu.agentbench.report/v1`, case digest, suite
  digest, all 8 metric entries (value or {null, reason}); Markdown writer
  renders metrics + failure class; **double-run byte equality** (JSON and MD);
  write failure → typed error, no partial silent file.
- GREEN: `report.{h,cpp}` (+ `json_writer.{h,cpp}`).

## Slice H — 20-case pack + suite runner + version pin
- RED: corpus-validation test — every case parses, unique ids, all six task
  families covered, ≥ 20 cases, budgets sane, fault cases declare expectation;
  suite report over the pack; suite doc digest pins the pack (changing a case
  changes the digest — version-pin test); pack regression: reference traces
  score stable metrics (golden values pinned in test).
- GREEN: `suite.{h,cpp}`, `data/agent/bench/**`, docs
  (`docs/agent/benchmark-harness.md`, `docs/integration.md`).

## Shipped deltas vs the slice plan (review pass 1 reconciliation)

- Fault mechanics shipped inside `fake_agent.cpp` (not `faults.{h,cpp}`);
  the recovery METRIC ships in `evaluator.cpp` (not `metrics.{h,cpp}`).
- Report tags shipped as `sicnu.agentbench.evaluation/v1` +
  `sicnu.agentbench.suite_report/v1` (not a single `report/v1`).
- `expected_evidence` may be empty (refusal/impossible tasks legitimately
  deliver nothing); the verifier compares id, kind, declared path and
  required fields.
- `impossible_task` stays in the closed taxonomy but is reserved for the
  live-capture seam; `failure_expectation` is advisory and surfaced in the
  evaluation document.
- Suite cap (400 entries) implemented in `runSuite`.
- Integer parsing gates on isInt64 + explicit ranges (no jsoncpp
  LogicError escapes); all serialization is to_chars-based (locale-free).
