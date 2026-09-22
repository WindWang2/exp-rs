# Dual review — Agent Ops Control Center

## Implementation review
- Compose-only: coordinator drives ScientificAgentSession; bridges enrich.
- Qt-free core + Qt UI/store writer split matches Feature B constraint.
- Autonomy gate covers mutating ops; non-Agent GUI paths untouched.
- Journal persist remains atomic (SessionJournal::save); unknown ≠ success.
- Secrets redacted; fault markers preserved; trace budget fail-closed.
- Parallel fences #1237–#1240 not modified.

## Adversarial review (findings → fixes)
1. **Json::Value::isMember on non-object** during cancel/terminal projection
   → fixed: guard with `isObject()` in live_session_recorder / ops_projection.
2. **LLM prose as sole root cause** → DiagnosticBridge refuses when no typed
   code; prose-only diagnose_run returns nullopt.
3. **Infinite self-heal** → RecoveryBridge enforces max retries/replans/repairs,
   no-progress, wall-clock, cancel.
4. **Duplicate successful submit on resume** → ResumeReconciler flags
   duplicateSubmitRisk; coordinator refuses delivered resume.
5. **UI bypassing loop** → controls expose `bypasses_loop=false`; mutations
   only via OperationsCoordinator / session_surface.
6. **AgentBench Qt contamination** → BenchmarkAdapter is Qt-free; store writer
   lives under `src/app/agent_ops`.
7. **#1240 hard dependency** → none; science_context not linked.
8. **Catch2 chained comparison** in cancel test → parenthesized.

## Known limits
- Production `agent_session_adapter` still absent; fake seams cover DoD.
- Repair planner schema projection only (no full rule-table planner on tip).
- Control Center panel not yet docked into MainWindow (hotspot avoidance);
  offscreen smoke + projection APIs ship.
- Full-repo cmake wiring added; lite harness validates Qt-free core.
- MCP tool registration into mcp_server.cpp deferred (central hotspot);
  session_surface documents the wire contract for drivers.
