# ARCHITECTURE — D17 pipeline designer layering

```
sicnu::agent::tools           (H) WorkflowOrchestratorTool / AgentCompiler
        │  compileGoalToWorkflow / healWorkflow            (pure, offline)
        ▼
sicnu::workflow  (A–E, new V2 layer, Qt Core/Gui only)
   workflow_ir_v2  →  workflow_dag_analyzer
        │                  │
        ▼                  ▼
   contract_checker → workflow_repair_engine
        │
        ▼
   plan_optimizer / workflow_cost_estimator
        │
        ▼
   pipeline_run_coordinator  (QThreadPool frontier scheduler,
        │                     two-phase atomic checkpoints, resume)
        ▼
sicnu::app::pipeline   (F) canvas: scene / node / port / connection items
sicnu::app::workbench  (G) GuidedWorkflowWidget (cards ⇄ canvas, 1 source of truth)
```

Rules:
- A–E,H depend on Qt6::Core (E adds Qt6::Concurrent-free QThreadPool) and
  nothing from qgis/processing/agent — the V2 layer stays embeddable.
- F,G depend on Qt6::Widgets + the A–E layer only.
- H depends on A–C (repair engine) only; no agent-harness linkage needed.
- Tests compile against public headers only; UI tests follow the
  test_guided_workflow_widget pattern (sources compiled into the target).
