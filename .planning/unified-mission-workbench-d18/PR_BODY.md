## Summary

D18 Unified Mission Workbench continuation on draft PR #991:

- Mission **publish** helpers (`publishMissionObject` / `publishMissionResultFromPath` / `setMissionActiveWorkflow`).
- **D14** `GeorefDualWindow` mounted in Raster → Image Registration menu, WorkbenchHost (`georef-dual`), and `workbench.georefDual`; `rectificationFinished` publishes Result (+ optional map load).
- **D15** `ClassificationStudioWidget` mounted in Analysis → Classification, WorkbenchHost (`classify-studio`), and `workbench.classifyStudio`; selection binds mission input; classification requests publish Result ids.
- **D17** IR 2.0 designer production-wired: canvas stack + `workflow_ir_v2.cpp` + `Ir2PipelineDesignerDock` in `sicnu_geo_rs` CMake (separate TU from Engine 2.0 to avoid dual `WorkflowDefinition` clash). Shared workflow id/fingerprint flows into `m_mission` and `workbench:context`.
- E2E scenarios 1–5 strengthened as headless MissionContext contracts.

## Baseline SHA

- `origin/master`: `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb`
- Branch: `grok/unified-mission-workbench-d18`

## Architecture

MissionContext remains a value aggregate under `src/app/workbench/`. Main window owns session `m_mission`. Specialist UIs retained; new mounts are additive.

## Authority / convergence decisions

See `DECISIONS.md` (D-W1/W2, D-M1–M4, D-I1–I3). No third scheduler. IR 2.0 rename still deferred.

## Major deliverables

- Audit + MissionContext (prior commits)
- Publish helpers + D14/D15/D17 mounts (this continuation)
- Tests: `test_mission_context`, `test_mission_e2e_scaffolding`

## D14–D17 integration findings

- D14 dual / D15 studio / D17 IR2 canvas now menu-reachable.
- Guided LabSpec lift + `PipelineRunCoordinator` start not yet production-wired (D-W2 follow-up).
- Dual `WorkflowDefinition` type name remains (compile-safe via TU separation).

## Compatibility

Additive. Classic I2I/I2M/classification windows kept.

## Tests

- Catch2 targets extended for publish + E2E contracts.
- **Local compile/ctest not executed on the agent box** (`cmake`/`g++` absent). See EVIDENCE.md.

## Performance / resource evidence

N/A for value-type / mount wiring; policy remains `-j2` / `CTEST_PARALLEL_LEVEL=1` when toolchain available.

## Review findings

See REVIEW_LOG.md. Mount P1 gaps cleared; no new P0.

## Known limitations

- Mission not yet in Qgs project XML (`DataProjectSerializer` follow-up).
- Classification studio Result is provisional until a path product exists (path publish API ready).
- IR2 dock does not yet start `PipelineRunCoordinator`.
- Full GUI E2E on toolchain host still pending.

## Follow-ups

1. Persist MissionContext with project save/restore.
2. Production-start PipelineRunCoordinator / Guided LabSpec from IR2 dock.
3. Rename IR 2.0 `WorkflowDefinition` → `WorkflowDocument`.
4. Run `ctest -R mission` on Qt6+cmake host.

## Evidence policy

Local evidence only; no online CI dependency (GOAL §18). Draft PR — **do not merge** until toolchain evidence lands.
