# ADR 0162 — Workflow IR 2.0, DAG Engine & Visual Pipeline Designer (D17)

- Status: Accepted (D17 track, branch `zcode/workflow-pipeline-designer`)
- Date: 2026-09-14
- Relates to: ADR 0149 (Scientific Workflow Compiler 10.0 — the IR 1.0 stack
  this document extends), ADR 0002 (worker pool sizing), issue #798 (no
  synchronous blocking in workers), issue #960 (atomic offline writes).

## Context

The platform has three workflow-like stacks that do not serve the new
node-graph designer use case:

1. **Workflow Engine 2.0** (`src/workflow/workflow_types.h`,
   `StepDef`/`WorkflowDefinition`): a jsoncpp, `std::string`-based session
   engine for guided TaskPanel pipelines; executed through TaskCenter via the
   production `WorkflowRunCoordinator` singleton.
2. **WorkflowIR 1.0** (ADR 0149, `src/agent/harness/workflow_ir.h`): the
   agent-side typed compiler document with artifact facts; never executes.
3. **GuidedWorkflowWidget** (`src/app/widgets/`): a LabSpec step-card
   renderer for teaching labs, wizard-only (no topology view).

None of these provides (a) a Qt-native, position-carrying document that a
*visual* node canvas can bind to directly, (b) concurrency-tier scheduling
with checkpoint/resume that an embedding widget can observe via Qt signals,
or (c) a deterministic natural-language → pipeline compiler with contract
self-healing. The D17 brief specifies this capability set as Workflow
IR 2.0 + a DAG engine + canvas + guided dual-view + agent orchestration.

## Decision

### 1. Workflow IR 2.0 — `sicnu::workflow` (new layer, files in `src/workflow/`)

A Qt-native property-graph document:

- `PortFact` — port name, data type, CRS, radiometric state, resolution,
  band count, required flag.
- `NodeFact` — node id, operator id, display name, parameter object,
  typed input/output ports, canvas position.
- `EdgeFact` — edge id, (source node, port) → (target node, port).
- `WorkflowDefinition` — versioned (`"2.0"`) container with `isValid`,
  `findNode`, `findEdge`.

Formal properties (enforced by code, pinned by tests):

- **Property graph**: every edge references existing nodes and existing port
  names on both ends (`validateSemantics` is fail-closed).
- **Single-source invariant**: an input port has in-degree ≤ 1; a second
  edge into the same port is a validation error.
- **Round-trip idempotence**: `S(D(J)) ≡ J` and `D(S(A)) ≡ A` for well-formed
  inputs — byte-identical under `QJsonDocument::Indented`.
- **V1 migration**: `migrateFromV1` accepts the ADR 0149 document
  (`kind: "workflow_ir"`), mapping ids/ports/facts, defaulting
  `radiometricState` and `canvasPosition`; malformed V1 fails closed.
- `Result<T>` — local expected-like value; no exceptions across the seam.

Why not extend IR 1.0 in place: IR 1.0 is deliberately harness-side,
`std::string`/jsoncpp, bound-limited, and consumed by the agent compiler; the
visual designer needs `QString`/`QJson` value types with canvas geometry and
value semantics for undo/redo. The two layers are adjacent, not merged; V2
defines the migration edge, keeping one direction of truth (V1 → V2).

### 2. DAG engine — `WorkflowDagAnalyzer`

- **Kahn in-degree tier partition**: T₀ = zero in-degree set; removing tier
  T_k decrements successors; tiers are concurrency units;
  C_max = max_k |T_k|; a non-empty residue proves the graph is not a DAG.
- **DFS 3-color cycle diagnosis**: White/Gray/Black; a Gray hit is a
  back-edge; the explicit ancestor stack yields the closed cycle path
  `[v, …, v]` for diagnostics. Detection is O(V+E) and bounded well under
  1 ms for pathological small cycles (test-pinned).

### 3. Contract checker & repair engine — closed rule table

Violations: CRS mismatch, resolution mismatch, radiometric-state mismatch,
data-type mismatch (dimension mismatch is enumerated but not derivable from
port facts today, so no rule fires for it). Repairs (shape-preserving,
auto-inserted): `rs:reproject` (target CRS, bilinear), `rs:resample`
(target resolution, bilinear), `rs:radiometric_calibration` +
`rs:atmospheric_correction` (DN → TOA/BOA chains), `rs:convert_dtype`.
Inserted adapters are named `adapter_<edgeId>_<operator>`. Determinism:
same workflow → byte-identical repaired workflow; inserted ids derived
deterministically. **Repair invariant**: re-inspecting a repaired workflow
yields an empty violation set (test-pinned). Unlike the 1.0 engine, the V2
repair table serves the *visual designer* use case where the operator
contract facts are explicit port facts, so auto-repair of radiometric chains
is permitted and recorded (the 1.0 decision-refusal policy remains scoped to
the knowledge-constrained agent path).

### 4. Plan optimizer & cost estimator

- `H(v) = SHA-256(opId ‖ canonical(params) ‖ ⊕^{sorted} H(parents))` —
  lineage signatures drive cache-hit reuse and CSE.
- DNE: keep only ancestors of the requested sinks.
- CSE: equal signatures merge; outgoing edges redirect.
- Cost model: Flops(v) = W·H·B·K(op); PeakRSS = max tier Σ W·H·B·4 B +
  base overhead; > 70 % host RAM ⇒ `recommendedMaxParallelism = 1`.

### 5. `PipelineRunCoordinator` — schedulable, checkpointed execution

- Instance-based `QObject` (not the TaskCenter-bound singleton): a designer
  canvas can own several coordinators; tests stay hermetic.
- Private `QThreadPool`; ready-set scheduling = the Kahn frontier; a node is
  dispatched iff all parents Succeeded; any parent Failed/Cancelled/Skipped ⇒
  Skipped cascade. Workers never block on other nodes (#798).
- **Two-phase checkpoint**: full status + lineage JSON →
  `checkpoint_<run_id>.json.tmp` → flush → fsync → atomic rename; emitted via
  `checkpointPersisted`.
- `resumeFromCheckpoint`: recomputes lineage signatures; a node is CacheHit
  iff signature matches **and** its recorded artifact exists on disk;
  otherwise recompute (crash-consistency, minimal recompute span).
- `requestCancel`: cooperative, drains running nodes to Cancelled.
- Node work is an injected `NodeExecutor`; D17 ships a deterministic
  synthetic raster executor (tests/E2E). JobEngine remains the production
  operator substrate; the coordinator is the workflow-level scheduler.

### 6. Visual canvas — `sicnu::app::pipeline`

`QGraphicsView`-based `PipelineCanvasWidget` + scene/node/port/connection
items. Connections are cubic Béziers with tangent control points
P₁=(x₀+δ, y₀), P₂=(x₃−δ, y₃), δ = max(30, 0.5·|x₃−x₀|). Port snapping at
Euclidean distance ≤ 12 px. Zoom clamped to [0.2, 3.0]. Rendering hints
(DeviceCoordinateCache, BoundingRectViewportUpdate) keep 100 nodes fluid.
The canvas binds directly to `WorkflowDefinition` (2.0) via
`loadWorkflow`/`exportWorkflow` — the document is the single source of
truth; graphics items are projections.

### 7. Guided workbench — `sicnu::app::workbench::GuidedWorkflowWidget`

Dual view over one document: `CardWizard` (LabSpec 1.0 step cards for
lab-tagged nodes, topology-ordered) and `TopologyCanvas` (embeds the
Package F canvas). `syncParameterToTopology` writes the document and emits
`parameterChanged` exactly once per edit (`ReentrancyGuard`, depth == 1).
Status colors: Pending `#E6A23C`, Running pulse I(t)=0.6+0.4·sin(2πt/1.5s),
Succeeded `#67C23A`, Failed `#F56C6C` with diagnostic tooltip.

### 8. Agent orchestration — `sicnu::agent::tools::WorkflowOrchestratorTool`

Deterministic NL-goal → DAG compilation (production rules over intent ×
sensor × inputs), Draft-07 tool schema, and `healWorkflow` mapping execution
error-log patterns (GDAL/PROJ CRS text, resolution text) onto the Package C
repair engine. The tool is pure and offline: no LLM round-trip is required
for compile or heal; an LLM front-end may *produce* the request, the mapping
itself is closed-form (testable, reproducible — same standard as the 1.0
compiler's rule table).

## Consequences

- New files only; no existing TU changes except additive registrations in
  `src/workflow/CMakeLists.txt`, `tests/CMakeLists.txt`, `.gitignore`
  (planning whitelist) and this ADR. The five seam paths that differ from
  the D17 brief are recorded in
  `.planning/workflow-pipeline-designer/DECISIONS.md` D1.
- Test targets are self-contained (Catch2 v3 with its bundled main via
  `Catch2::Catch2WithMain`, minimal Qt/jsoncpp links), offscreen-capable,
  `ctest -j1`-safe.
- The designer stack is sandbox-safe: pure value types, no network, no
  registry writes; artifacts land in caller-provided run directories.
- Risk: divergence between IR 1.0 and 2.0 semantics is contained by the
  one-way migration edge and its tests; the migration is the only coupling.
