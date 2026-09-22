# ADR 0175: Evidence-first Agent Session — one bounded, auditable loop

Status: accepted · Branch `agent/rs14-evidence-first-agent-loop` · Baseline `origin/master@4f6632e1f6`

## Context

Everything a scientific agent needs already exists and works: the harness
compiler (ADR 0149) plans deterministically, scientific preflight gates
execution over resolved facts, the closed repair rule table inserts
geometry-only repairs with evidence, artifact verification can never be
bypassed into a success, `harness:diagnose_run` proposes structured
repairs with a bounded budget, and the workflow engine plus TaskCenter run
durable, budgeted pipelines. What does **not** exist is the layer that
runs one **session** end to end:

1. **No loop owner.** Each stage is an independent tool call; nothing owns
   the transitions, the retry budget, or the "we are going in circles"
   detection.
2. **No structured decisions.** `ContextLedger`'s decision rows are a
   manual, shallow continuity ledger — there is no record of *inputs,
   alternatives, selected action, reason, evidence, policy* per key
   decision of an automated run.
3. **No machine-readable evidence.** A human watching the GUI sees what
   happened; a future agent has no versioned document to consume.
4. **No run modes.** There is no "tell me what you would do without
   doing it".

The 13.0 mission runtime records *who runs a task* but never launches
one — deliberately (the mission layer stays read-only for this track), so
the orchestration gap is not filled there either.

## Decision

Add an orchestration layer that **drives** the existing capabilities
behind interfaces and records every decision, without reimplementing any
of them and without touching the engine.

1. **Pure core library `sicnu_agent_loop`** (`src/agent_loop/`, C++20 +
   jsoncpp, no Qt): the closed stage vocabulary, the typed transition
   table, `DecisionRecord`, the replayable `SessionJournal`, the seam
   interfaces, the session policy (budgets/modes), deterministic offline
   doubles, and the `ScientificAgentSession` driver. Keeping it Qt-free
   makes the whole loop testable headless in seconds and keeps the
   teaching-mode and agent-mode semantics provably identical.
2. **Stages** — `goal_normalization → data_state_snapshot → plan_request →
   preflight → (repair_approval) → execute → verify → (diagnose →
   replan)* → delivery`, with `delivered / refused / aborted` terminals.
   Illegal transitions are typed errors that never mutate state (the
   mission-timeline discipline).
3. **DecisionRecords are the unit of auditability.** Every key decision
   carries inputs, the alternatives that were NOT taken (and why), the
   selected action, the reason, evidence pointers, and the policy that
   governed the choice. Records live in the session journal — the single
   source for orchestration decisions. Nothing writes to `ContextLedger`
   (that stays the manual continuity store); nothing here becomes a
   second provenance or experiment store.
4. **Budgets are policy, checked before they are needed.** Replan limit,
   no-progress detection keyed on the attempt-independent plan identity
   plus the failure code, resource budget against the plan's declared
   estimates, a hard step limit, a bounded executor wait, and cooperative
   cancellation. A policy that cannot be bounded refuses the session
   before it starts.
5. **Modes.** `dry_run` reports the would-execute plan (executor and
   verifier never invoked — asserted through call counters); `plan_only`
   stops after a clean preflight; `execute_with_verify` runs the full
   loop. A FAIL verification can never surface as delivered success, in
   any mode.
6. **Repairs are classified by one table and recorded by the session.**
   The harness repair engine is the single classifier: it auto-inserts
   only `shape_preserving` repairs (each with an `IrRepairRecord` naming
   the facts used) and turns `radiometric` / `science_changing` proposals
   into refusals, never insertions. Both insertion records and refusals
   reach the session as proposals; the session's repair-approval policy
   then gates them wherever they appear — preflight, an ok-verdict
   compile, or a diagnosis: a declared-safe class records an approval
   decision (and rides into the next plan); anything else is recorded as a
   withheld, decision-required repair, and the run refuses unless the
   policy explicitly allows proceeding unfixed (itself a recorded
   decision). No repair is ever silently applied: the compiler's record
   and the session's DecisionRecord both exist, or the repair does not
   happen. The production planner re-derives repairs deterministically
   from the same compiler on every attempt, so the approved-repair list is
   the audit trail of what was allowed into the plan, not a second
   insertion mechanism.
7. **The production adapter** (`src/agent/tools/agent_session_adapter.*`)
   implements the seams over the real harness (compiler, preflight,
   verifier, evidence sidecars) and the authoritative engine
   (`WorkflowRunCoordinator` → `TaskCenter`, bounded wait) — the single
   scheduler seam. `scientific:agent_session` is a thin projector tool
   over it: it owns no business logic. The teaching role is normalized
   through `harness_actions::normalizeLabRole` before it reaches the
   policy (one role table); a student in the lab domain gets
   `TEACHING_REFUSAL`, structurally, never a soft apology.
8. **Restart where safe.** A *cancelled* session (and only a cancelled
   one — budget/no-progress aborts and refusals are absorbing) resumes
   from its persisted journal, and only from a stage whose inputs are
   re-derived from the request (`goal_normalization`,
   `data_state_snapshot`): everything from `plan_request` onward
   dispatches on in-memory state the journal does not carry as values
   (plan draft, snapshot, preflight report, execution outcome), so a
   resume there would run real seams over default-constructed state and
   fabricate a delivery — the session must restart instead. The machine
   rewinds to the journal's final stage with the replan count restored;
   the sequence, the decision numbering, the logical clock, the step
   count, and the journalled goal are rebuilt from the journal (at the
   pre-plan stages the failure keys and approved repairs are empty by
   construction); no seam is re-invoked for work already recorded. A
   resumed session must restate the journalled goal — a different goal
   is refused (`SESSION_GOAL_MISMATCH`). The data-state snapshot is
   re-taken as a read-only fact step (the harness session store's
   staleness philosophy). (Amended on hardening/agent-harness-session-autonomy
   to describe the resume contract actually shipped in RS14-11.)

## Consequences

- The loop is provably bounded and auditable: every stop is typed
  (`PREFLIGHT_BLOCKED`, `OUTPUT_INVALID`, `SESSION_NO_PROGRESS`,
  `SESSION_REPLAN_LIMIT`, `RESOURCE_OVER_BUDGET`, `TEACHING_REFUSAL`,
  `CANCELLED`, …); there is no silent fallback path.
- Agents (and students) consume one machine-readable artifact: the
  versioned `EvidenceSummary` plus the replayable journal.
- The core adds zero runtime cost to the GUI: the tool is registered by
  `registerAgentSessionTools()` and the core is a static library.
- The adapter's diagnoser is deliberately conservative (typed root cause,
  no invented proposals); forwarding `harness:diagnose_run`'s structured
  proposals into the session is the documented extension point in
  `docs/integration.md`.
- The mission-layer launch gap (mission records runs, never starts them)
  is explicitly NOT filled here; wiring a session to advance a mission
  timeline is a future integration point, also documented.
