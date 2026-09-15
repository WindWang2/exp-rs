# PLAN — workflow-pipeline-designer (D17)

Vertical slices only: each slice is seam header → failing Catch2 test →
minimal implementation green → atomic commit. No horizontal bulk slicing.

## Public seams & I/O contracts

### Package A — `src/workflow/workflow_ir_v2.{h,cpp}` (`sicnu::workflow`)
- `struct PortFact { QString portName, dataType, crs, radiometricState; double resolutionX/Y; int bandCount; bool isRequired; }`
- `struct NodeFact { QString nodeId, operatorId, displayName; QJsonObject parameters; QVector<PortFact> inputPorts, outputPorts; QPointF canvasPosition; }`
- `struct EdgeFact { QString edgeId, sourceNodeId, sourcePortName, targetNodeId, targetPortName; }`
- `struct WorkflowDefinition { QString version="2.0", workflowId, name, description; QVector<NodeFact> nodes; QVector<EdgeFact> edges; QJsonObject metadata; isValid() / findNode() / findEdge(); }`
- `class WorkflowIR { fromJson(toJson(validateSemantics(migrateFromV1 }`.
- `Result<T>` minimal expected-like value.
- Tests `tests/test_workflow_ir_v2.cpp`: 10 golden JSON fixtures
  (linear / diamond / band-split-merge / post-classification / temporal cube /
  minimal-1-node / malformed×3 / V1 doc); round-trip byte identity
  (`QJsonDocument::Indented`), single-source in-degree ≤ 1, dangling-edge
  rejection naming the offender, V1 migration defaults.

### Package B — `src/workflow/workflow_dag_analyzer.{h,cpp}`
- `ConcurrencyTier{tierIndex, nodeIds}`, `DagAnalysisResult{isAcyclic, executionTiers, linearSchedule, cyclePath, errorMessage}`.
- `analyzeDag / detectCycleDFS / computeConcurrencyTiers / calculateMaxParallelism`.
- Tests `tests/test_workflow_dag_analysis.cpp`: diamond hand-truth
  T₀={S}, T₁={A,B}, T₂={M}, C_max=2; 3-cycle + downstream truth
  `cyclePath == [N1,N2,N3,N1]`; linear chain schedule; disconnected node;
  self-loop; 100-node 10-tier synthetic (tiers sizes verified, µs-scale
  cycle check); empty graph.

### Package C — `src/workflow/contract_checker.{h,cpp}` + `workflow_repair_engine.{h,cpp}`
- `ContractMismatchType{Crs, Resolution, RadiometricState, DataType, Dimension}Mismatch`,
  `ContractViolation`, `inspectContracts`.
- `RepairAction`, `RepairPlan`, `WorkflowRepairEngine{inspectContracts, inferRepairs, applyRepairPlan}`.
- Repair invariant `inspect(applyRepairPlan(W, inferRepairs(W))) == ∅` as a test.
- Tests `tests/test_workflow_repair_rules.cpp`: CRS mismatch (EPSG:4326 DN →
  EPSG:32649 reflectance slope operator) injects exactly one `rs:reproject`;
  DN→BOA injects calibration+atmospheric chain; resolution gap injects
  `rs:resample`; `*` wildcard CRS passes; clean pipeline → no violations;
  chained multi-adapter case order.

### Package D — `src/workflow/plan_optimizer.{h,cpp}` + `workflow_cost_estimator.{h,cpp}`
- `computeNodeSignature(node, parentSignatures)` — SHA-256 over
  opId ‖ canonical params ‖ sorted parent signatures.
- `optimizePlan(def, targetSinks, outReport)` — DNE (reverse reachability),
  CSE (equal signatures merge, edges redirected), cache-hit annotation.
- `estimatePipelineCost(def, rasterDimensions)` — Flops = W·H·B·K(op),
  PeakRSS = max tier Σ W·H·B·4 + base overhead, waterline 0.70 →
  `recommendedMaxParallelism = 1`.
- Tests `tests/test_workflow_cost_estimator.cpp`: signature determinism +
  param sensitivity + parent-order invariance; DNE=2/CSE=1 on the redundant
  fixture (node math −3); 1000×1000×4b pipeline ≈ 96 MiB within 15 %;
  oversize grid forces parallelism 1.

### Package E — `src/workflow/pipeline_run_coordinator.{h,cpp}`
- `ExecutionState{Pending,Ready,Running,Succeeded,Failed,Cancelled,Skipped}`,
  `NodeStatusSnapshot`, signals `nodeStatusChanged/nodeFinished/pipelineCompleted/checkpointPersisted`.
- `startRun(def, runDirectory) / requestCancel() / resumeFromCheckpoint(path) / getAllStatuses()`.
- Injected `NodeExecutor` (`operatorId`, params, inputs → artifact path);
  D17 synthetic raster executor for tests (D4).
- Tests `tests/test_workflow_checkpoint_cache.cpp`: single-node success;
  10-step chain with injected failure at node 5 → downstream all Skipped;
  resume → nodes 1–4 `isCacheHit`, 5 re-runs; cancel mid-run → Cancelled;
  checkpoint file is valid JSON, `.tmp` absent after persistence.

### Package F — `src/app/pipeline/pipeline_canvas_widget.{h,cpp}` + `pipeline_node_item`, `pipeline_port_item`, `pipeline_connection_item`, `pipeline_scene`
- `PipelineCanvasWidget : QGraphicsView { loadWorkflow, exportWorkflow, setZoomLevel [0.2,3.0], zoomFitExtent }`,
  signals `connectionCreated/nodeSelected/requestAutoLayout`.
- `PipelineConnectionItem::calculateBezierSpline(p0,p3)` static pure function;
  δ = max(30, 0.5·Δx); port snap radius 12 px.
- Tests `tests/test_pipeline_canvas_widget.cpp`: analytical midpoint
  B(0.5)=(200,150)±0.5 for (100,100)→(300,200); zoom clamp; loadWorkflow
  round-trip via exportWorkflow (nodes/edges/positions); 100-node load < 50 ms
  offscreen; snapping predicate hit/miss at 11.9/12.1 px.

### Package G — `src/app/pipeline/guided_workflow_workbench.{h,cpp}` (`sicnu::app::workbench`)
- `ViewMode{CardWizard, TopologyCanvas}`, `LabStepCard`,
  `GuidedWorkflowWidget{loadLabSpec, setViewMode, syncParameterToTopology,
  underlyingWorkflow}`, signals `parameterChanged/executionTriggered`,
  `ReentrancyGuard` depth == 1.
- Tests `tests/test_guided_workflow_sync.cpp`: load shipped
  `data/labs/lab02_spectral_analysis.lab.json`; parameter edit from card side
  and from topology side both land in the single `WorkflowDefinition`; no
  signal echo loop; completed-step projection order follows topology tiers.

### Package H — `src/agent/tools/workflow_orchestrator_tool.{h,cpp}` + `workflow_agent_compiler.{h,cpp}`
- `AutonomousCompileRequest/Result`, `getToolJsonSchema()` (Draft-07-valid),
  `compileGoalToWorkflow(request)` — deterministic production rules for ≥5
  intents (calibrate+index, water extraction, change detection, fusion,
  classification); `healWorkflow(broken, errorLog)` — regex error patterns →
  repair engine.
- Tests `tests/test_workflow_agent_tools.cpp`: JSON Schema shape; each intent
  → expected operator chain (exact node count + required node ids present);
  CRS-mismatch log heals to contract-clean workflow with
  `rule_crs_auto_reproject` recorded; unknown log → no-op result, no false
  success.

### Package I — `tests/test_d17_workflow_pipeline_e2e.cpp`
- Slice 1: 3-node mini E2E (IR → analyze → optimize → run → artifacts exist).
- Slice 2: crash consistency — run 10-node chain, abort after ~50 %,
  resume, assert first half CacheHit + final all Succeeded.
- Slice 3: 100-node / 10-tier × 10 all Succeeded under 1.5 GiB peak RSS
  (measured via getrusage ru_maxrss); all 11 shipped
  `data/labs/*.lab.json` run green through the full stack
  (data-driven glob, DYNAMIC_SECTION per lab).

## Ground-truth register (anti-tautology)

| Property | Independent source |
|---|---|
| Tier partition of diamond | hand-derived set algebra in test |
| Cycle path | hand-traced DFS stack |
| Bézier midpoint | closed-form Bernstein evaluation in the test |
| SHA-256 | precomputed literal for a fixed known input (sha256sum) |
| Cost model | arithmetic in the test from the formula constants |
| Skip cascade | enumerated expected state vector |
| Golden JSON round-trip | fixture files under `tests/fixtures/workflow_ir_v2/` |
| Lab set | `QDir(data/labs).entryList("*.lab.json")` — data-driven |

## Milestones / commit cadence

Each package = 2–3 atomic commits (`feat(d17-A): …` style). Phases end with
a full targeted `ctest -R "test_d17_workflow|test_workflow|test_pipeline|test_guided" -j1`
green record in EVIDENCE.md.
