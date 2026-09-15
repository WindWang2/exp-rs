## Summary

D18 Unified Mission Workbench continuation on draft PR #991:

- MissionContext **dual-write** on project save/open: `<stem>.mission.json` sidecar + `sicnuMissionContext` project XML (sibling of governance block; **not** inside DataProjectSerializer).
- **D17 IR2 dock** starts `PipelineRunCoordinator` (Run/Cancel) and can load Guided **LabSpec** into the same IR 2.0 document / `ActiveWorkflowRef`; publishes `WorkflowRun` on completion.
- `WorkflowDocument` alias for IR 2.0 (full `WorkflowDefinition` rename deferred — large D17 surface).
- **D15** classification Results are path-backed when products exist (`classificationProductReady`, classic load-to-map, artifact_paths / class-layer reuse); provisional ids only as fallback.
- Prior mount slice retained: D14 dual / D15 studio / D17 canvas menus + publish helpers.

## Baseline SHA

- `origin/master`: `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb`
- Branch: `grok/unified-mission-workbench-d18`

## Architecture

MissionContext remains a value aggregate under `src/app/workbench/`. Main window owns session `m_mission`. Persistence is dual-write (sidecar primary on restore). Specialist UIs retained; IR2 dock owns its coordinator instance (no third scheduler).

## Authority / convergence decisions

See `DECISIONS.md` (D-W1–W4, D-M1–M5, D-I1–I4).

## Major deliverables

- Audit + MissionContext + mounts (prior commits)
- Project persist dual-write + IR2 run/LabSpec + classify path Results (this continuation)
- Tests: `test_mission_context`, `test_mission_e2e_scaffolding` (extended contracts)

## D14–D17 integration findings

- D14/D15/D17 menu-reachable (prior).
- IR2 → PipelineRunCoordinator start path now wired; LabSpec lift available from dock.
- Dual `WorkflowDefinition` name: alias `WorkflowDocument` introduced; rename still deferred.

## Compatibility

Additive. Classic I2I/I2M/classification windows kept. Governance serializer unchanged.

## Tests

- Catch2 targets extended for dual-write, path-product upgrade, IR2 run identity.
- **Local compile/ctest not executed on the agent box** (`cmake`/`g++` absent). See EVIDENCE.md.

## Performance / resource evidence

N/A for value-type / mount wiring; policy remains `-j2` / `CTEST_PARALLEL_LEVEL=1` when toolchain available.

## Review findings

See REVIEW_LOG.md. Persist P1 cleared; no new P0.

## Known limitations

- IR2 runs use D17 **synthetic** node executor until production operators are bound.
- Full IR 2.0 type rename (`WorkflowDefinition` → `WorkflowDocument` across D17) still deferred.
- Guided LabSpec **cards** UI (`GuidedWorkflowWidget`) not required for dock LabSpec load.
- Full GUI E2E on toolchain host still pending.

## Follow-ups

1. Bind real operators into PipelineRunCoordinator from the dock (replace synthetic executor).
2. Dedicated IR 2.0 `WorkflowDocument` rename / namespace split PR.
3. Optional Qgs custom-property mirror if XML+sidecar insufficient for some hosts.
4. Run `ctest -R mission` on Qt6+cmake host.

## Evidence policy

Local evidence only; no online CI dependency (GOAL §18). Draft PR — **do not merge** until toolchain evidence lands.
