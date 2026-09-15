# AUDIT_WORKFLOW — Phase A: workflow authority & conversion graph

Code-derived (master @ `ebcafb4d` + D14–D17 merges). Not aspirational.

## Competing representations found

| # | Name | Location | Namespace / wire | Responsibility |
|---|------|----------|------------------|----------------|
| 1 | **Workflow Engine 2.0 definition** | `src/workflow/workflow_types.h` `struct WorkflowDefinition` | `sicnu::workflow`, jsoncpp | Guided TaskPanel / session steps (`StepDef`, gates, host kind). Executed by `WorkflowRunCoordinator` → TaskCenter |
| 2 | **Workflow IR 1.0** | `src/agent/harness/workflow_ir.h` | `sicnu::agent::harness`, `kind: "workflow_ir"`, schema `1.0` | Agent compiler document with artifact facts. Never executes. Lowers to AgentPlan |
| 3 | **AgentPlan v2** | `src/agent/harness/agent_plan.h` | `kind: "execution_plan"`, schema `2.0` | Slot-filled plan; `compilePlanToWorkflowJson` → Engine 2.0 JSON |
| 4 | **Workflow IR 2.0 document** | `src/workflow/workflow_ir_v2.h` **also named** `struct WorkflowDefinition` | `sicnu::workflow`, QJson, version `"2.0"` | Designer property-graph (ports, canvas positions). Used by DAG analyzer, contract checker, repair, optimizer, `PipelineRunCoordinator` |
| 5 | **LabSpec workflow** | lab loaders + `labspec_workflow_lift` / `GuidedWorkflowWidget` (widgets/) | LabSpec 1.0 JSON | Teaching step cards; D17 lifts to IR 2.0 |
| 6 | **Legacy pipeline editor model** | `src/app/workflow/*` (PipelineEditorDock / PipelineScene) | Engine 2.0 `WorkflowDefinition` | Older canvas over Engine 2.0 steps (still mounted in main window docks) |
| 7 | **D17 pipeline canvas** | `src/app/pipeline/*` | IR 2.0 `WorkflowDefinition` | New node-graph + guided dual-view — **compiled in tests; not listed in `src/app/CMakeLists.txt` production app sources** |
| 8 | **Recipe / cartography recipe** | cartography docs + recipe authoring | separate | Map product recipes — not a scientific DAG authority |
| 9 | **Compiled workflow JSON** | output of `compilePlanToWorkflowJson` / `workflowDefinitionToJson` | Engine 2.0 serialization | Execution-plane payload |

## Critical naming collision

Two distinct types share the C++ name `sicnu::workflow::WorkflowDefinition`:

- Legacy: `workflow_types.h` (std::string / StepDef)
- IR 2.0: `workflow_ir_v2.h` (QString / NodeFact / EdgeFact)

They never co-include in one TU today (D17 TUs include only `workflow_ir_v2.h`; Engine TUs include `workflow_types.h`). **This is a compile landmine** and a documentation hazard. Convergence strategy (see `DECISIONS.md` D-W1): keep both for this slice; introduce an alias / rename of the IR 2.0 type to `WorkflowDocument` (or similar) in a dedicated follow-up commit once call sites are inventoried — do **not** delete Engine 2.0.

## Actual conversion graph (from code)

```text
Natural language / LabSpec / recipe_id
        │
        ▼
┌───────────────────────────────┐
│ Agent harness                 │
│  WorkflowIR 1.0  (ADR 0149)   │──migrateFromV1──► Workflow IR 2.0 document
│  compileWorkflow               │                   (designer AST)
└──────────────┬────────────────┘
               │ lower
               ▼
        AgentPlan v2
               │ compilePlanToWorkflowJson
               ▼
   Engine 2.0 WorkflowDefinition JSON
               │
               ▼
   WorkflowRunCoordinator ──► TaskCenter / ExecutionPlane
               │
               ▼
        WorkflowRun + checkpoints (Engine 2.0)

Parallel designer path (D17):
   IR 2.0 document ──► PipelineRunCoordinator (private QThreadPool,
                        injectable NodeExecutor; NOT TaskCenter)
```

### Authority boundaries

| Concern | Authority | Projection / adapter |
|---------|-----------|----------------------|
| Agent scientific compilation | IR 1.0 + AgentPlan | `compileWorkflow`, `compilePlanToWorkflowJson` |
| Production scheduled execution | Engine 2.0 + `WorkflowRunCoordinator` + TaskCenter | Session / run checkpoints |
| Visual designer document | IR 2.0 `WorkflowIR` | `migrateFromV1`; canvas / guided workbench |
| Designer-local run / resume | `PipelineRunCoordinator` | Checkpoints under run directory; synthetic executors in tests |
| Teaching LabSpec | LabSpec JSON | Lift → IR 2.0 (`labspec_workflow_lift`); also legacy GuidedWorkflowWidget |

### Information loss / divergence notes

- IR 1.0 artifact-fact vocabulary is richer than IR 2.0 `PortFact` (modality, determinism, wavelengths). Migration defaults radiometric state and canvas grid; facts beyond PortFact are thinned.
- Engine 2.0 steps lack typed CRS/radiometric ports; AgentPlan verification policies do not round-trip into IR 2.0 ports.
- Two run coordinators: production TaskCenter bridge vs D17 instance-based pool (intentional per D17 DECISIONS D3) — D18 must not invent a third scheduler; Agent/UI must agree which document+run id is active (MissionContext).

### Desired D18 outcome (not yet fully implemented)

One **explicit** authority model with compatibility projections:

1. IR 2.0 = designer / shared Agent↔UI document when a visual workflow is open.
2. Engine 2.0 = execution substrate for TaskCenter-backed runs.
3. IR 1.0 = agent compiler input; always liftable to 2.0 when entering the designer.
4. MissionContext holds `workflowDocumentId` + `workflowFingerprint` + `workflowRunRef` — never live canvas pointers.
