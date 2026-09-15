## Summary

D18 Unified Mission Workbench continuation on draft PR #991:

- MissionContext **dual-write** on project save/open: `<stem>.mission.json` sidecar + `sicnuMissionContext` project XML (sibling of governance block; **not** inside DataProjectSerializer).
- **D17 IR2 dock** starts `PipelineRunCoordinator` (Run/Cancel) and can load Guided **LabSpec** into the same IR 2.0 document / `ActiveWorkflowRef`; publishes `WorkflowRun` on completion.
- **Operator bind (D-W5):** dock installs `makeRegistryNodeExecutor()` — nodes with registry `operatorId` execute via `RSOperatorRegistry`; empty/unknown ids **typed-refuse** (`ir2.operator_unbound:`). Synthetic default remains only for hermetic D17 tests (no `setExecutor`).
- `WorkflowDocument` alias advanced on IR2 surface (canvas / LabSpec lift / guided / `startRun`); Engine 2.0 untouched.
- **D15** classification Results are path-backed when products exist; provisional ids only as fallback.
- Prior mount slice retained: D14 dual / D15 studio / D17 canvas menus + publish helpers.

## Baseline SHA

- `origin/master`: `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb`
- Branch: `grok/unified-mission-workbench-d18`

## Architecture

MissionContext remains a value aggregate under `src/app/workbench/`. Main window owns session `m_mission`. Persistence is dual-write (sidecar primary on restore). Specialist UIs retained; IR2 dock owns its coordinator instance (no third scheduler). Node work uses existing `NodeExecutor` + `RSOperatorRegistry`.

## Authority / convergence decisions

See `DECISIONS.md` (D-W1–W5, D-W3b, D-M1–M5, D-I1–I4).

## Major deliverables

- Audit + MissionContext + mounts (prior)
- Project persist dual-write + IR2 run/LabSpec + classify path Results (prior)
- IR2 registry executor bind + WorkflowDocument IR2 call-site rename (this continuation)
- Tests: `test_mission_context`, `test_mission_e2e_scaffolding` (extended contracts)

## D14–D17 integration findings

- D14/D15/D17 menu-reachable (prior).
- IR2 → PipelineRunCoordinator start path wired; LabSpec lift available from dock.
- Operator plane: registry bind on dock; unbound refuse; synthetic only off dock default path.
- Dual `WorkflowDefinition` name: alias used on IR2 surface; Engine 2.0 struct rename still deferred.

## Compatibility

Additive. Classic I2I/I2M/classification windows kept. Governance serializer unchanged. Engine 2.0 headers untouched.

## Tests

- Catch2 contracts for dual-write, path-product, IR2 run identity, registry bind policy.
- **Local compile/ctest not executed on the agent box** (`cmake`/`g++` absent). See EVIDENCE.md.

## Performance / resource evidence

N/A for value-type / mount / executor wiring; policy remains `-j2` / `CTEST_PARALLEL_LEVEL=1` when toolchain available.

## Review findings

See REVIEW_LOG.md. Prior R2 synthetic-on-dock cleared; no new P0.

## Known limitations

- Unbound / not-linked operator ids refuse at run time (honest); they are **not** silently synthetic.
- Full ExecutionPlane/TaskCenter path remains Engine 2.0 (D-W1) — IR2 does not replace it.
- Full IR 2.0 struct rename (`WorkflowDefinition` → `WorkflowDocument` across all D17 TUs) still deferred.
- Guided LabSpec **cards** UI optional for dock LabSpec load.
- Full GUI E2E on toolchain host still pending.

## Follow-ups

1. Optional: richer port-name → param mapping for multi-input operators beyond primary `input`.
2. Dedicated IR 2.0 struct rename / namespace split PR (beyond alias call sites).
3. Optional Qgs custom-property mirror if XML+sidecar insufficient for some hosts.
4. Run `ctest -R mission` on Qt6+cmake host; GUI smoke of bound vs unbound Run.

## Evidence policy

Local evidence only; no online CI dependency (GOAL section 18). Draft PR — **do not merge** until toolchain evidence lands.
