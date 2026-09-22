# Architecture — Agent Ops Control Center

## Compose graph
`OperationsCoordinator` drives `ScientificAgentSession` (authority for stages).
Bridges enrich; they do not replace the loop:

| Component | Feature | Authority composed |
|-----------|---------|-------------------|
| LiveSessionRecorder | A | SessionJournal → agentbench.trace/v1 |
| BenchmarkAdapter (+ Qt writer) | B | suite_report → ExperimentStore |
| DiagnosticBridge | C | verifier/preflight/runtime/debugger/diagnose |
| RecoveryBridge | D | diagnostic → retry/repair/replan/ask/abort |
| autonomy_ops_gate | E | decideAutonomy on mutating ops |
| ResumeReconciler | F | journal resume; unknown ≠ success |
| DeliveryAssembler | G | FinalDelivery + capsule export |
| OpsProjector + Control Center UI | H | timeline; controls cannot bypass loop |
| session_surface | I | MCP/Pi drivers of same spine |

## Parallel fences
Never touch #1237–#1240 paths. Optional science_context adapter is out of scope.

## Rollback
Revert this branch / close PR; root CMake `add_subdirectory` lines for
`agent_loop`, `repair_planner`, `agent_ops`, `app/agent_ops` are additive.
