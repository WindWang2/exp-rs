# RS14-11 Evidence-first Agent Loop — Slices (Phase 2)

Each slice: RED (failing test first) → confirm it fails for the right reason → GREEN (minimal) → REFACTOR → narrow tests green → commit. One logical slice per commit.

## Slice A — Core values, state machine, journal
- `src/agent_loop/agent_loop.h` (umbrella), `session_state.h/.cpp` (stages, transitions, typed results), `decision_record.h/.cpp` (DecisionRecord + JSON + fail-closed version reader), `session_journal.h/.cpp` (append/bound/evict/atomic persist/replay).
- Tests: `tests/test_agent_loop_core.cpp`.
  - stage vocabulary closed; legal transition table; illegal transition → typed error, no mutation;
  - DecisionRecord round-trip; unknown schema_version rejected; missing required fields rejected;
  - journal: append order, seq monotonic, bound+eviction, persist→load identity, replay determinism (two replays byte-identical), corrupt document → typed error.
- Commit: `feat(agent_loop): session state machine, decision records, replayable journal`.

## Slice B — Seams, fakes, policy/budgets
- `session_seams.h` (IDataStateProvider/IPlanner/IPreflight/IExecutor/IVerifier/IDiagnoser + value types), `session_policy.h/.cpp` (RunMode, budgets, repair approval policy, teaching policy), `fake_seams.h` (deterministic fakes — header-only, shipped for tests AND offline exemplar).
- Tests: `tests/test_agent_loop_seams.cpp`.
  - fakes are deterministic (same inputs → same outputs, twice);
  - policy defaults; invalid policy values rejected (max_replans<0, threshold<1, unknown mode);
  - repair approval: auto class approved; non-listed class → decision-required refusal; unknown class → refused (fail closed).
- Commit: `feat(agent_loop): seam interfaces, session policy, deterministic fakes`.

## Slice C — Session driver: dry-run / plan-only
- `scientific_agent_session.h/.cpp`: run(goal) driver through goal_normalization → data_state_snapshot → plan_request → preflight; modes dry_run (stop after snapshot+plan sketch? no — dry_run runs the full pipeline WITHOUT executing: it may plan+preflight and report what WOULD happen) and plan_only (stop after preflight, deliver plan + decisions).
- Tests: `tests/test_agent_loop_modes.cpp`.
  - dry_run never invokes executor; delivers evidence summary with would_execute plan;
  - plan_only stops at preflight ok; terminal state delivered; no verify/execute entries in journal;
  - plan_only with preflight blocked → refused, typed reason, executor never invoked;
  - each key stage emits ≥1 DecisionRecord with non-empty reason + evidence.
- Commit: `feat(agent_loop): session driver with dry-run and plan-only modes`.

## Slice D — execute → verify → success E2E
- Executor seam begin/poll wired into the driver; verifier stage; delivery on PASS / PASS_WITH_WARNINGS.
- Tests: `tests/test_agent_loop_e2e.cpp`.
  - happy path: fake planner → executor → verifier PASS → delivered; journal shows full stage chain in order; summary machine-readable fields present;
  - PASS_WITH_WARNINGS → delivered with warning recorded (not silently upgraded);
  - verify FAIL can never surface as delivered-success;
  - teaching student + lab domain: executor gate refuses → terminal refused + TEACHING_REFUSAL decision; agent role identical otherwise (parity);
  - determinism: two runs → identical journal JSON (modulo session id/clock injection fixed).
- Commit: `feat(agent_loop): execute-verify delivery path with offline E2E`.

## Slice E — verify fail → diagnose → bounded replan
- Diagnoser seam; replan loop bounded by max_replans; repair approval policy applied to proposals; new plan fingerprint differs from failed one.
- Tests: extend `tests/test_agent_loop_e2e.cpp`.
  - verify FAIL → diagnosis → approved repair (auto class) → replan → verify PASS → delivered; replan count and both plans in journal;
  - FAIL → diagnosis proposing non-auto repair → decision-required → no silent application → replan WITHOUT that repair or refused, with DecisionRecord;
  - FAIL → diagnosis with no proposals → refused (typed OUTPUT_INVALID), no infinite loop;
  - max_replans exhausted → aborted (SESSION_REPLAN_LIMIT), typed.
- Commit: `feat(agent_loop): diagnose and bounded replan loop`.

## Slice F — no-progress / resource budget / abort
- No-progress detection keyed on (plan fingerprint, failure code) repetitions; resource budget vs plan estimates; user abort via cancellation token.
- Tests: extend e2e file.
  - same fingerprint+code repeated ≥ threshold → aborted SESSION_NO_PROGRESS even under replan budget;
  - differing fingerprint each replan → no false no-progress;
  - plan estimate over resource budget → aborted SESSION_BUDGET_EXCEEDED before execution;
  - cancel token set mid-session → aborted CANCELLED at next stage boundary; no executor call after cancel.
- Commit: `feat(agent_loop): budget, no-progress detection, cancellation`.

## Slice G — replay/restart, adapter, tool, docs, exemplar
- Journal replay API (`replayJournal` → history + decisions, no seam invocation); restart-resume where safe (resume a non-terminal session from journal at its last stage — deterministic, seams re-injected).
- Production adapter `src/agent/tools/agent_session_adapter.*`: seams over harness (`compileWorkflow`, `preflightIntent`, `verifyArtifact`, evidence sidecars) + engine (`WorkflowRunCoordinator` begin/poll); `scientific:agent_session` tool (plan_only/dry_run/execute_with_verify) registered via `registerAgentSessionTools()`.
- Targeted adapter test (offline, no engine): planner/preflight/verifier seams produce the same documents as direct harness calls for a fixed fixture (parity, not re-testing harness).
- `docs/adr/0173-evidence-first-agent-session.md`, `docs/integration.md` wiring points, teaching exemplar fixture + doc.
- Commit(s): adapter; docs; exemplar.
