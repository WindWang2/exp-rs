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
