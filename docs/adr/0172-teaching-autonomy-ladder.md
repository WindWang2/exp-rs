# ADR 0172 — Teaching Autonomy Ladder: the L0–L5 Gate Is Code, Before the Action

- Status: accepted (this track: RS14-12-teaching-autonomy)
- Date: 2026-09-21
- Extends: ADR 0155 (lab copilot — the teaching constraint is a code-level
  gate), Harness 4.0 tool manifest (risk classes), D9 evaluation suite.

## Context

The lab copilot (D9, ADR 0155) can diagnose, hint, and explain, and is
structurally unable to hand a student a finished artifact. What it cannot do
is **graduate** that behavior: a course that wants "concept hints only during
the exam", a teacher who wants "no next-step hints for lab 3 this week", or an
agent-domain user who explicitly wants full L5 autonomy all need the same core
capability under different amounts of assistance. Hiding buttons in the UI is
not a control: the agent surfaces (MCP `tools/call`, CLI, routed tools) do not
render the UI at all.

The repo already had every ingredient except the ladder itself: a closed
intent vocabulary, a closed risk-class table (tool_manifest), a role model with
a fail-closed default, a typed error taxonomy, and two proven gate patterns
(the teaching constraint in `harness_actions`, the MeteredTool decorator at the
registry choke point). No autonomy level, policy schema, mode switch, decision
engine, or decision audit existed anywhere.

## Decision 1 — One closed ladder; capabilities are the unit of decision

`L0 no_assistance · L1 concept_hint · L2 error_localization ·
L3 next_step_recommendation · L4 plan_generation · L5 autonomous_execution`,
plus `read_only_query` (L0) as the capability projection of query-class tools.
Every assistance intent and every tool/action risk class classifies onto this
closed taxonomy; classification is fail-closed (unknown input classifies as
unknown and is denied — never as a more capable class). The taxonomy is
versioned with the policy schema, so an old policy document cannot authorize a
capability it never knew about.

## Decision 2 — A versioned policy schema with one merge rule

`sicnu.autonomy-policy/1` carries `level`, `mode`, `max_level`, and
per-capability `capability_overrides`. Four sources resolve with a fixed
precedence (`course < labspec < teacher < session`): per field, the
highest-precedence declaring source wins; `max_level` ceilings take the
tightest declared value, so no source can loosen another source's cap; unknown
sources are ignored. `resolveEffectivePolicy` is the ONLY merge rule — there
is no second place where precedence is decided. Parsing is strict and total:
malformed documents are refused with typed errors and never partially applied;
a refused install keeps the previous policy.

Modes bound the ladder: `exam` ≤ L2, `practice` ≤ L4, `instructor` ≤ L5,
`agent` = the explicit L5 opt-in for the research domain (no mode ceiling;
scientific verification stays mandatory when execution is allowed).

## Decision 3 — Two gates, both before the action, both audited

1. **Assistance** — inside `labAsk()`, after role normalization and intent
   classification: the intent classifies onto a capability, the effective
   policy resolves from course + labspec + session layers, and the engine
   decides. A deny returns a typed refusal envelope carrying the reason code
   and a Chinese explanation; a downgrade routes the answer to the capability
   the effective level does unlock (the student still gets help — just not the
   next step itself), and every answer carries an `autonomy` block.
2. **Execution** — inside `ExecutePlanTool::execute()`, before preflight,
   compile, and submit: the highest blast-radius single call in the harness
   (one call = N mutations), and the one seam the lab `routed_tool` path, MCP
   `tools/call`, and the CLI all reach. The risk class comes from the harness
   tool manifest (the single source of truth); a refused plan never reaches
   the workflow engine.

Session context (role, domain, policy overrides) is host-injected; the tool
input schemas deliberately omit those fields, exactly like `role`, so a
composing model is never invited to claim authority. The session layer can
RAISE a level, so it is privileged: it is honored only together with the
host-injected teacher credential (`SICNU_LAB_TEACHER_TOKEN` — the same gate
the teacher surfaces use). A self-injected `autonomy` block without the
credential is ignored and the session keeps its course/labspec policy;
restrictions live in the course and labspec layers and never need the block.

## Decision 4 — Structural rules outrank overrides; failures are typed

- Unknown capability ⇒ deny `AUTONOMY_UNKNOWN_CAPABILITY`.
- Lab domain + student + execution ⇒ deny `AUTONOMY_LAB_STUDENT_EXECUTION`
  (ADR 0155 restated on the autonomy axis; no override lifts it).
- Non-lab execution requires `mode=agent` ⇒ else `AUTONOMY_AGENT_MODE_REQUIRED`;
  when allowed, `verification_required=true` — OutputVerifier and
  harness_verification are untouched and stay the downstream control.
- Assistive capabilities above the effective level **downgrade** to the
  highest unlocked capability; L0 denies (a "read-only" answer is still help);
  execution above the level **denies** and names the binding constraint
  (`AUTONOMY_MODE_CEILING` / `AUTONOMY_COURSE_CAP` / `AUTONOMY_LEVEL_TOO_LOW`)
  — execution is never silently substituted with a weaker action.
- The nine reason codes join the closed harness error taxonomy
  (validation / retry none): a refusal is never retryable, because the same
  request under the same policy can never succeed.
- Every decision is recorded in a bounded (1000, FIFO), thread-safe audit log
  (`sicnu.autonomy-decision/1`) with monotonic sequences and no wall-clock —
  identical request sequences replay byte-identically.

## Decision 5 — A Qt-free leaf, and no second source of truth

The whole policy layer is a static library over jsoncpp only
(`Sicnu::autonomy`), so the semantics are testable without the GUI/QGIS link.
Risk classes, lab roles, verification, and the action table stay where they
are and are passed IN; the autonomy module re-declares none of them — the gate
suite pins the mirror (risk strings, role rule) against the live authorities
so drift fails a test. Fake action providers (deterministic corpora, no LLM,
no network) drive every policy test.

## Consequences

- A course can express "exam = error localization at most" as data, and the
  refusal a student sees names the binding constraint in machine-readable form.
- Pre-existing research flows are behavior-compatible: the default course
  policy is the research default (L5/agent); the gates are present but inert
  until a teaching host installs a restrictive policy.
- The teaching constraint is now enforced twice on two axes (artifact-producing
  action keys in harness_actions; capability level here) — both structural,
  both typed, both tested against bypass attempts.
