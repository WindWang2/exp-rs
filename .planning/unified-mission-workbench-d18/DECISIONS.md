# DECISIONS — Unified Mission Workbench D18

`autonomy=full`: decisions taken from code + ADRs; recorded here.

## D-W1 — Workflow authority: three layers, one Mission handle

**Context.** Master has Engine 2.0, IR 1.0, and IR 2.0 (plus LabSpec / recipes). IR 2.0 reused the name `WorkflowDefinition` in `sicnu::workflow`.

**Decision.**
- **Designer / Agent↔UI shared document** = IR 2.0 (`workflow_ir_v2`).
- **Production execution** = Engine 2.0 via `WorkflowRunCoordinator` → TaskCenter (no third scheduler).
- **Agent compiler input** = IR 1.0; enter designer through `WorkflowIR::migrateFromV1`.
- `PipelineRunCoordinator` remains the designer-local / testable runner (D17 D3); MissionContext records which runner class is active.
- Rename IR 2.0 type to `WorkflowDocument` deferred to a dedicated commit after MissionContext lands (avoid mixing rename churn with the first slice).

**Consequences.** MissionContext stores workflow id + schema version + content fingerprint + optional run ref — not a second IR.

## D-M1 — MissionContext lives under `src/app/workbench/`

**Context.** Workbench 10 already owns `WorkbenchObjectRef`, `SelectionContext`, `workbench:context`.

**Decision.** Add `mission_context.{h,cpp}` beside `object_identity.*`. MissionContext is a **value aggregate of typed refs and extents** (QString ids, QJson-friendly). It does **not** hold `QObject*`, `QgsMapLayer*`, or QPointer.

**Consequences.** App + headless tests can round-trip JSON without QGIS widgets. Live resolution always re-queries `ProjectContext` / stores by id.

## D-M2 — Reuse `WorkbenchObjectRef` instead of parallel Ref types

**Decision.** Mission lists (`datasets`, `layers`, `results`, `models`, `workflowRuns`, `experiments`, `assets`) are `QVector<WorkbenchObjectRef>`. Spatial/Temporal/Selection sub-contexts are plain structs with string ids + numeric extents.

**Consequences.** Agent and UI share one identity vocabulary (GOAL §6).

## D-M3 — Persistence: sidecar JSON key under project, not live DOM pointers

**Decision.** First slice: `MissionContext::toJson` / `fromJson` with schema `mission_context` version `1.0`, plus `contentFingerprint()` (SHA-256 via `QCryptographicHash` over canonical JSON). Integration with `DataProjectSerializer` / project file is a follow-up once the value type is stable.

## D-I1 — Specialist UIs stay; consume MissionContext by bind API

**Decision.** Do not remove Qgs georeferencer / classification windows or TemporalWorkbenchPanel. Add optional `bindMissionContext(MissionContext *)` / snapshot apply helpers. Prefer publishing outputs as asset/result ids into the mission over path-only reload.

## D-I2 — First studio integration target: SelectionContext + Temporal panel facts

**Decision.** First consumer: build/update MissionContext from `SelectionContextSnapshot` + explicit temporal fields; expose a bounded `missionSummaryJson()` for Agent. ClassificationStudioWidget gets a thin `setMissionInputRef(WorkbenchObjectRef)` so the studio can record the mission input without owning layers.

## D-B1 — No online CI; toolchain may be absent

**Decision.** If cmake/Qt missing on the agent box, still land sources + CMake/test wiring; record non-execution in `EVIDENCE.md`. Do not wait on GitHub Actions.

## D-I3 — Menu-mount D14 dual / D15 studio; keep classic windows

**Decision.** Add menu + WorkbenchHost + CommandRegistry entries for `GeorefDualWindow` and `ClassificationStudioWidget` without removing classic I2I/I2M/classification windows. Studio outputs publish via `publishMissionResultFromPath` / Result refs; path load remains a convenience side effect.

## D-W2 — Production-wire IR 2.0 canvas as separate dock TU

**Decision.** Add `app/pipeline/*` canvas + `workflow_ir_v2.cpp` + `Ir2PipelineDesignerDock` to `sicnu_geo_rs`. Do **not** include Engine 2.0 `workflow_definition.h` in those TUs (D-W1 name clash). Guided LabSpec lift / PipelineRunCoordinator remain optional follow-ups (extra deps: lab_spec_loader, dag analyzer, plan_optimizer).

## D-M4 — Session MissionContext on main window

**Decision.** `QgisDesktopWindow` owns `m_mission` value. `workbench:context` merges selection into `m_mission` and re-emits the session summary so Agent sees studio publishes + IR2 fingerprint.

## D-M5 — MissionContext dual-write: sidecar + sibling XML (not DataProjectSerializer)

**Context.** Sidecar `.mission.json` already existed. GOAL Phase J needs project save/reopen. Embedding inside `sicnuDataManager` risks coupling to Workspace Governance v3 downgrade guards.

**Decision.** Dual-write on `QgsProject::writeProject` / `readProject`:
1. **Primary:** `<stem>.mission.json` beside the project file.
2. **Portable mirror:** sibling root element `sicnuMissionContext` (JSON text, version attr `1`) so `.qgs`/`.qgz` carry mission without the sidecar.
On restore: prefer sidecar when present; else XML. Missing both = fresh mission (not an error).

**Consequences.** DataProjectSerializer remains governance-only. Documented in mission_context_store.h.

## D-W3 — IR 2.0 `WorkflowDocument` alias; full rename deferred

**Context.** Engine 2.0 and IR 2.0 both use `sicnu::workflow::WorkflowDefinition` (~34 IR2 call-site files). Full rename is high churn.

**Decision.** Add `using WorkflowDocument = WorkflowDefinition` in `workflow_ir_v2.h`. IR2 surface call sites use the alias (D-W3b). Full struct rename / namespace split remains deferred until a dedicated migration PR.

## D-W4 — IR2 dock owns PipelineRunCoordinator + LabSpec lift

**Decision.** `Ir2PipelineDesignerDock` constructs a child `PipelineRunCoordinator`, exposes Run/Cancel, and can `liftLabSpecToWorkflow` into the same document identity (`ActiveWorkflowRef.runner = pipeline_run_coordinator`). Installs `makeRegistryNodeExecutor()` (D-W5). Main window publishes `ObjectKind::WorkflowRun` on `pipelineRunFinished`.

## D-I4 — Classification Result path upgrade

**Decision.** Studio keeps provisional request ids only when no product path exists. `classificationProductReady` / `acceptProductPath`, classic `requestLoadToMainMap`, and reuse of existing `artifact_paths` / class-named layers publish path-backed Results via `publishMissionResultFromPath`.


## D-W5 — IR2 NodeExecutor binds RSOperatorRegistry (typed unbound refusal)

**Context.** D17 `PipelineRunCoordinator` defaulted to a synthetic node executor so designer/checkpoint tests stayed hermetic. D18 dock was starting runs with that default — production-looking success without operators.

**Decision.**
1. Add `makeRegistryNodeExecutor()` in `ir2_registry_node_executor.*` (extends existing `NodeExecutor` + `RSOperatorRegistry`; no new scheduler).
2. `Ir2PipelineDesignerDock` always `setExecutor(makeRegistryNodeExecutor())`.
3. **Bound:** `node.operatorId` present in `RSOperatorRegistry` → create/execute with QJson params + parent artifacts (`input` / `output` defaults under the run directory).
4. **Unbound:** empty or unknown `operatorId` → fail with `ir2.operator_unbound:…` (never synthetic success on the dock path).
5. **Synthetic retained** only as coordinator default when `setExecutor` was never called (D17 hermetic tests) and via `makeSyntheticNodeExecutor()` for explicit test installs.

**Remaining non-production paths (documented, not silent).**
- Designer/LabSpec nodes whose `operatorId` is missing from the process registry (teaching placeholders, typos, not-yet-linked operator TUs) refuse — they are **not** synthetic.
- Operator runtime failures use `ir2.operator_failed:…` (params/IO/compute); that is honest execution-plane feedback, not a bind gap.
- Full ExecutionPlane / TaskCenter bridge remains Engine 2.0 `WorkflowRunCoordinator` (D-W1); IR2 stays on `PipelineRunCoordinator`.

## D-W3b — WorkflowDocument call-site rename on IR2 surface

**Decision.** Advance D-W3: IR2 app surface (`pipeline_canvas_widget`, `labspec_workflow_lift`, `guided_workflow_workbench`, `PipelineRunCoordinator::startRun` param) uses the `WorkflowDocument` alias. Struct name in `workflow_ir_v2.h` and Engine 2.0 `WorkflowDefinition` remain unchanged (no cross-TU clash fix beyond the alias).


## D-W6 — Multi-input IR2 port→param mapping (prefer explicit names)

**Context.** D-W5 registry executor only filled primary `params["input"]` from a sorted parent-node-id map and dumped `ir2_input_artifacts` keyed by source node id. Multi-input operators (`reference`, `dem`, `mask`, `inputA`/`inputB`, …) could not bind inbound edges by IR2 port name.

**Decision.**
1. `PipelineRunCoordinator` keys `inputArtifacts` by **target port name** (IR2 `EdgeFact.targetPortName`), not source node id. Single-source invariant ⇒ one edge per input port.
2. Pure helper `applyIr2InputPortMapping` (hermetic TU) maps each bound port → `params[portName]` when unset; writes `params["ir2_input_artifacts"]` as port→path; aliases primary `input` from port `"input"`, else first declared bound port, else lexicographic first port.
3. Explicit `node.parameters` always win (never overwritten).
4. Legacy source-node-id keys: order-only zip onto declared input ports (sorted keys × declaration order) — documented fallback, not preferred.
5. Unbound refusal (`makeIr2UnboundRefusal` / `ir2.operator_unbound:`) still runs **before** mapping; mapping cannot invent success.

**Limitations (honest).**
- Does **not** introspect `RSOperator::schema()` to rename ports to differently named params; designers should name IR2 input ports to match operator param ids (`input`, `reference`, `dem`, …).
- Order-only legacy zip is deterministic but ambiguous when fan-in count ≠ declared port count.
- Full ExecutionPlane/TaskCenter bridge remains Engine 2.0 (D-W1).
