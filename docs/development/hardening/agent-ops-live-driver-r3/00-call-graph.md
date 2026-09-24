# Agent Ops Live Driver R3 — real call graph and seam status

Branch: `hardening/r3-agent-ops-live-driver-r3` (base `origin/master` `5697ca2ad`).
Scope: agent_loop → agent_ops → driver entries (CLI / MCP / Workbench), resume-cancel-recovery
closed loop. agent_loop stays the ONLY state machine; TaskCenter/ExecutionPlane stay the
execution authority; nothing here re-implements either.

## 1. The real call graph (verified on this branch)

```
CLI `session <action>` (src/cli/cli_agent_ops_commands.cpp)
MCP `scientific:agent_session` (src/agent/mcp_server.cpp → handleAgentSession)
Workbench (src/app/agent_ops/agent_ops_control_center_panel.h — signals exist,
           not yet connected to a driver; see §4)
        │
        ▼
OpsDriver::apply (src/agent_ops/ops_driver.cpp)          [NEW]
  • per-session bookkeeping (status/timeline/export keyed by session_id)
  • seam-completeness gate (SEAMS_UNAVAILABLE before any loop work)
  • duplicate-submit guard (SUBMITTED_RUN_EXISTS / DUPLICATE_SUBMIT_REFUSED)
  • reconcile action (inspect a journal without resuming)
        │ passthrough + run/resume
        ▼
sessionSurfaceApply / sessionSurfaceStatus (src/agent_ops/session_surface.cpp)
        │
        ▼
OperationsCoordinator::run / resume (src/agent_ops/operations_coordinator.cpp)
  • pause/cancel atomics; autonomy gate on ExecuteWithVerify (run AND resume now)
  • budget application (full parity now)
  • crash-evidence checkpoint sink → LiveSessionRecorder::persistJournal [NEW]
  • BridgedDiagnoser, RecoveryBridge (advisory), DeliveryAssembler, OpsProjector,
    ResumeReconciler, BenchmarkAdapter
        │
        ▼
ScientificAgentSession::run / resume (src/agent_loop/scientific_agent_session.cpp)
  goal_normalization → data_state_snapshot → plan_request → preflight →
  (repair_approval) → execute → verify → (diagnose → replan)* → delivery
  • cooperative cancel: requestCancel() checked at every stage boundary and
    after executor poll (executor->cancel first)
  • journal appended per stage/decision; checkpoint sink invoked per append [NEW]
        │ seams only (src/agent_loop/session_seams.h)
        ▼
IExecutor / IVerifier / IPlanner / IPreflight / IDiagnoser / IDataStateProvider
  • IVerifier: HarnessVerifier (src/agent/tools/agent_session_adapter.cpp) [NEW]
    → harness::verifyArtifact (src/agent/harness/harness_verification.cpp) — REAL
  • IExecutor (production): NOT wired — TaskCenter/ExecutionPlane remain the
    execution authority behind TaskCenter-only surfaces (MCP execute_algorithm,
    WorkflowRunCoordinator); an agent session without an injected executor
    refuses (typed) instead of faking execution.
```

## 2. Crash / restart safety boundaries (this branch)

| Kill point | Evidence left | Restart behavior |
|---|---|---|
| pre-plan (goal/snapshot) | checkpointed journal prefix on disk | resume() continues (RESUMABLE) |
| plan / preflight / repair_approval | checkpointed prefix | resume refused: `RESUME_PAST_PLAN_SEAM`; restart as new session |
| execute, after submit decision | prefix contains decision `action="run"`, `inputs["run_id"]` | resume refused `RESUME_PAST_PLAN_SEAM` **and** `duplicateSubmitRisk=true` + submitted run ids on the wire; driver `run --session-id` refused (`SUBMITTED_RUN_EXISTS`) |
| post-run / delivered | terminal journal | resume refused `DUPLICATE_SUBMIT_REFUSED` |
| unknown / corrupt journal | — | `CORRUPTED_OR_MISSING_JOURNAL` / `INDETERMINATE_STATE` — unknown is never resumable |

The checkpoint sink is invoked after EVERY journal append, so the on-disk prefix is always
a loadable snapshot of the loop's evidence (loop-level contract: `test_agent_loop_e2e`
checkpoint suites; coordinator-level: `test_agent_ops_core` crash suites).

## 3. Repair approval / ask flow (this branch)

* The loop's `withhold_repair` decisions (radiometric / science-changing, never
  auto-approved) surface as typed `questions` on `FinalDelivery` and in the capsule export —
  the ask can no longer die inside the decision log.
* `RecoveryBridge.projectRepairPlan` now resolves each action's risk class from the
  diagnostic's per-proposal evidence (`proposal_details`), falls back to the leading risk
  class, and defaults to `science_changing` — unknown is never presented as
  `shape_preserving`. Every projected action carries `evaluateRepairPolicy`'s verdict in
  `plan.policy.actions[]`; ops projections never carry an executable action key, so nothing
  in the plan document is ever `auto_executable`.
* `approve_repair` (surface) → one-shot pending approval consumed by the next launch;
  science-changing stays `needs_confirmation` at the policy layer even with approval —
  approval arms a human decision, never an automatic execution.

## 4. Known limits / not done here

* Production planner / preflight / executor / diagnoser seams over
  `compileWorkflow` / `preflightIntent` / `WorkflowRunCoordinator` / `harness:diagnose_run`
  are NOT wired. A host without them gets typed `SEAMS_UNAVAILABLE` refusals from
  `session run/resume` (never a fake session). Wiring them end-to-end (incl. a
  PlanDraft→AgentPlan→workflowJson mapping and kill/restart oracles against the live
  engine) is the next track-sized slice.
* The CLI/MCP entries are live for discovery, status, export, reconcile, and all control
  actions; the app does not yet construct an OpsDriver at startup (the MCP tool fails
  closed with `AGENT_OPS_UNAVAILABLE` until `McpServer::setAgentOpsDriver` is called by the
  app bootstrap).
* The workbench control-center panel (src/app/agent_ops) is intentionally untouched — a
  parallel PR (#1312) owns the shared app shell files; the panel's signals can bind to an
  OpsDriver later without core changes (PR #1312's PR #1286 panel remains test-only).
* `OperationsCoordinator::run()` is synchronous; status mid-run is out of scope (the loop's
  single-threaded state machine is the authority; a live-status API would be a new seam).
