# RS14-11 Evidence-first Agent Loop — Architecture Plan (Phase 1)

## 1. Problem statement

exp-rs already owns every *capability* an autonomous scientific agent needs: a deterministic
planner/compiler (ADR 0149), scientific preflight, a closed repair rule table, artifact
verification, structured diagnosis, a durable workflow engine, a scheduler, and a mission
timeline. What it lacks is the **orchestration layer that runs one bounded, evidence-first
session end to end**: normalize the goal, snapshot the data state, request a plan, preflight
it, apply an explicit repair-approval policy, execute through the authoritative engine, verify
the outputs, diagnose failures, replan within bounds, and deliver a machine-readable evidence
summary — recording a typed `DecisionRecord` at every key decision.

Without it, each of those stages is reachable only as an independent tool call; nothing owns
the *loop*, the *budget*, or the *audit trail*.

## 2. User stories

**Undergraduate experiment (teaching mode).** A student states a goal ("compute NDVI for this
Sentinel-2 scene and tell me whether the result is usable"). The session normalizes the goal,
snapshots the dataset, plans, and — instead of silently producing an artifact — returns an
evidence-first narration: what data was understood, which decision points existed (band roles?
calibration state?), which were auto-resolved and why, what the verifier checked and found, and
what remains uncertain. The student sees the *scientific process*, not a magic button. Under the
existing teaching gate (student role + lab domain) artifact-producing execution is withheld with
`TEACHING_REFUSAL` — the session records the refusal as a decision instead of failing silently.

**AI agent (agent mode).** A future agent consumes one machine-readable surface: submit a goal
with a run mode (`dry_run` | `plan_only` | `execute_with_verify`), receive the session journal
and the final evidence summary as versioned JSON — every decision with its inputs, alternatives,
reason, evidence, and policy; every verification verdict; every budget consumption. The agent can
replay a session deterministically from the journal.

## 3. Architecture

```
                 ┌────────────────────────────────────────────────────┐
                 │            sicnu_agent_loop (pure C++20)           │
                 │                                                    │
  goal ────────► │  ScientificAgentSession  (FSM + loop + budgets)    │
                 │     │                                              │
                 │     ├─ DecisionRecord      (typed, versioned)      │
                 │     ├─ SessionJournal      (append-only, replay)   │
                 │     ├─ SessionPolicy       (budgets, modes, gates) │
                 │     └─ EvidenceSummary     (delivery projection)   │
                 │                    │                               │
                 │            session_seams.h (interfaces)            │
                 └────────────────────┬───────────────────────────────┘
                                      │  implemented by
        ┌─────────────────────────────┼──────────────────────────────┐
        │                             │                              │
  fakes (tests, offline)     production adapter              mission/workflow/
  deterministic planner      (src/agent/tools/               taskcenter (read/
  in-memory stores           agent_session_adapter.*)        observe only)
```

**Core library** `src/agent_loop/` → `sicnu_agent_loop`: STATIC, C++20, jsoncpp only, no Qt, no
GDAL, no QGIS. Unit tests link in seconds and run fully offline.

**Production adapter** `src/agent/tools/agent_session_adapter.*` + `scientific:agent_session`
SpatialTool (in `sicnu_agent`, Qt-capable): implements the seams over the real harness
(`compileWorkflow`, `preflightIntent`, `verifyArtifact`, evidence sidecars) and the authoritative
engine (`WorkflowRunCoordinator` / `TaskCenter`) — the only place that touches live systems.

**Non-negotiables (from the track boundaries):**
- The session never schedules work itself; the executor seam is the single path to the engine.
- The session never inserts repairs; it approves/records them per policy and replans.
- The session never silently corrects scientific state; every correction is a DecisionRecord.
- Mission/Workflow/TaskCenter sources are untouched.

## 4. Public API / data schema

All persisted documents carry `schema_version` (fail-closed reader: unknown major → typed error).

### 4.1 Stages (closed vocabulary)

```
goal_normalization → data_state_snapshot → plan_request → preflight →
repair_approval → execute → verify → diagnose → replan → … → delivery
```
Terminal states: `delivered` (success), `refused` (blocked/unsafe/teaching gate),
`aborted` (budget/no-progress/user cancel). Illegal transitions are typed errors, never
mutations (same discipline as `MissionTimeline`).

### 4.2 `DecisionRecord` (the core value type)

```json
{
  "schema_version": "1.0",
  "decision_id": "decision-<seq>",
  "session_id": "...",
  "stage": "preflight",
  "recorded_at": 12345,                 // injected logical clock (ms)
  "inputs":     { "…": "facts the decision consumed" },
  "alternatives": [ { "id": "...", "description": "...", "why_not": "..." } ],
  "selected":   { "action": "...", "…": "…" },
  "reason":     "why the selected action was chosen",
  "evidence":   [ { "kind": "verification|preflight|fact|policy|engine", "ref": "..." } ],
  "policy":     { "policy_id": "session_policy", "version": "1.0" }
}
```

### 4.3 `SessionJournal`

Append-only, bounded (`kMaxEntries = 4096`, oldest-first eviction), atomic file persistence
(temp + rename), deterministic replay (journal → full history; seams are NOT re-invoked on
replay). Entry: `{seq, at, stage, event, payload?, decision?}`.

### 4.4 Seams (`session_seams.h`, pure virtual)

- `IDataStateProvider::snapshot(goal, refs) → DataStateSnapshot`
- `IPlanner::plan(PlanRequest) → PlanDraft` (carries `planFingerprint`-style identity, estimates, dropped alternatives, missing facts)
- `IPreflight::check(PlanDraft, DataStateSnapshot) → PreflightReport` (`ok|fixable|blocked`, issues, repair proposals with risk class)
- `IRepairApprover` — NOT a seam; the approval policy is a value (`RepairApprovalPolicy`): auto-approve only listed risk classes; anything else becomes a recorded decision-required refusal. (The teaching gate is enforced by the executor seam in production — `harness_actions::teachingGateBlocks` — and by an injected gate in tests; the session only records the outcome.)
- `IExecutor::begin(PlanDraft) → ExecutionStart` / `IExecutor::poll(start, timeout) → ExecutionOutcome` (bounded wait; fakes are synchronous)
- `IVerifier::verify(outputs, expectations) → VerificationReport` (tri-state PASS / PASS_WITH_WARNINGS / FAIL)
- `IDiagnoser::diagnose(failure evidence) → Diagnosis` (typed root-cause code + bounded repair proposals)

### 4.5 `SessionPolicy` (budgets & modes)

```json
{
  "mode": "dry_run | plan_only | execute_with_verify",
  "max_replans": 3,
  "no_progress_threshold": 2,     // same (plan fingerprint, failure code) repetitions
  "resource_budget_mb": 0,        // 0 = unbounded; else sum(plan estimates) must fit
  "max_steps": 64,                // hard loop bound
  "executor_timeout_ms": 300000,
  "repair_approval": { "auto_approve_risk_classes": ["shape_preserving"] },
  "teaching": { "intent_domain": "", "role": "" }   // "" => agent semantics
}
```

### 4.6 `EvidenceSummary` (delivery)

Versioned JSON: goal, mode, outcome, per-stage timeline, decisions, verification verdicts,
budget consumption, artifacts, and typed stop reason. This is the machine-readable deliverable
for future agents.

## 5. Migration / compatibility

Nothing existing changes. New files only:
`src/agent_loop/*` (core), `src/agent/tools/agent_session_adapter.*` (adapter + tool),
`tests/test_agent_loop_*.cpp` (unit/E2E), `docs/adr/0173-*`, `docs/integration.md` section.
No schema migration; no registry replaces another. If `scientific:agent_session` registration
trips a drift gate, only test-owned tables are updated (never weakened).

## 6. Observability

- Journal entries are structured and bounded; `EvidenceSummary` is the query surface.
- Every terminal state carries a typed reason code (from the harness taxonomy where applicable:
  `PREFLIGHT_BLOCKED`, `EXECUTION_FAILED`, `OUTPUT_INVALID`, `RESOURCE_OVER_BUDGET`,
  `TEACHING_REFUSAL`, plus session-level codes `SESSION_NO_PROGRESS`, `SESSION_BUDGET_EXCEEDED`,
  `SESSION_REPLAN_LIMIT`).
- No prose-only failures: unsafe/unknown/unsupported always produce typed results.

## 7. Security / trust boundary

- The session is a *driver*: it cannot escalate privileges beyond the seams it is given.
- Path/fact handling stays with the provider seam (the production provider reuses the existing
  workspace-containment rules; the core never touches the filesystem except journal persistence
  under a caller-supplied directory).
- Teaching gate is structural (executor seam), never prompt-based; the session records refusals.
- Deterministic replay reads only the journal; replay can never re-execute work.

## 8. Performance budget

- Core operations are O(entries) with bounded structures; journal capped at 4096 entries /
  1 MiB document (compaction projection drops payload bodies first, mirroring
  `HarnessSessionStore::compactState`).
- No polling tighter than the executor seam's own cadence; executor waits are bounded by
  `executor_timeout_ms`.
- Unit tests must not link QGIS/GDAL (pure lib) — build cost stays ~seconds.

## 9. Test strategy (per core behavior: happy / invalid / boundary / persistence / replay / cancellation / mode parity)

| Behavior | Tests |
|---|---|
| FSM transitions (legal/illegal, terminal) | unit |
| DecisionRecord schema + round-trip + fail-closed version | unit |
| Journal append/bound/evict/atomic persist/replay determinism | unit |
| Modes: dry_run / plan_only / execute_with_verify | unit + E2E |
| Happy path E2E (fake planner → execute → verify PASS → delivered) | E2E |
| Preflight blocked → refused (typed, no execution) | unit + E2E |
| Verify FAIL → diagnose → approved repair → replan → success (bounded) | E2E |
| Verify FAIL → diagnosis with no approvable repair → refused | E2E |
| Replan budget exhausted → aborted (typed) | E2E |
| No-progress detection → aborted | E2E |
| Resource budget exceeded → aborted | E2E |
| Teaching mode (student) withholds execution; agent mode identical otherwise | E2E parity |
| Adapter: production seams wire to real harness functions (plan-only/dry-run) | targeted |
| Determinism: same inputs → byte-identical journal/summary | unit |

Oracle potency rules: oracles recompute expectations independently where possible; no
"any-terminal" assertions; vacuous passes are treated as defects (issue #1179 discipline).

## 10. Work packages (slices)

A. Core values + state machine + journal (RED→GREEN)
B. Seam interfaces + fakes + policy/budgets
C. dry-run / plan-only modes
D. execute → verify → success E2E (offline planner)
E. verify fail → diagnose → bounded replan
F. no-progress / resource budget / abort
G. replay/restart + adapter + tool surface + docs/ADR + exemplar

## 11. Rollback / kill-switch

- Core lib is additive; removing the directory + CMake lines restores the tree.
- The tool is registered by an explicit `registerAgentSessionTools()` call; not calling it
  disables the surface with zero behavior change.
- No persisted state of existing components is touched; journal files live under the harness
  session directory convention with their own subdirectory.

## 12. Definition of Done (track-specific)

1. Teaching E2E: an evidence-first session a student can read (decisions, verifier findings,
   uncertainties) — demonstrated by a fixture exemplar, offline.
2. Machine-readable interface: `EvidenceSummary` + journal JSON + `scientific:agent_session`
   tool consumable by a future agent.
3. Single source of truth: no duplicated planner/preflight/repair/verify/diagnose logic; the
   adapter delegates.
4. Explainable failure: every stop is typed; no silent fallback anywhere.
5. Offline: all core tests and the exemplar run without network.
6. Bounded resources: budgets, journal bounds, executor timeouts, sampling limits declared.
7. Dynamic dedup re-verified against latest master/open issues before PR.

## 13. Self-review of this plan

- **Second parallel truth source?** No — decisions live only in the session journal; nothing
  writes to ContextLedger/provenance stores. Documented boundary.
- **Bypassing registry/provenance?** No — executor goes through the authoritative engine;
  verification consumes existing sidecars.
- **Implicit science correction?** No — repairs are approved/recorded, never auto-applied by
  the session.
- **GUI business logic?** None added; the tool is a thin projection.
- **"Can call but cannot verify" tools?** The session always pairs execution with verification
  (execute_with_verify) or refuses to claim success (plan_only/dry_run never report success).
- **Student over-automation?** Teaching gate withholds artifact production; dry_run/plan_only
  are the teaching default.
- **Typed errors / versioned schema / deterministic persistence / bounded perf / issue
  avoidance / sibling overlap?** Addressed in §6, §4, §8, and the recon dedup matrix.
