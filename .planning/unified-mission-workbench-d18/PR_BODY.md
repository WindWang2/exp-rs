## Summary

D18 Unified Mission Workbench continuation on draft PR #991:

- MissionContext **dual-write** on project save/open: `<stem>.mission.json` sidecar + `sicnuMissionContext` project XML (sibling of governance block; **not** inside DataProjectSerializer).
- **D17 IR2 dock** starts `PipelineRunCoordinator` (Run/Cancel) and can load Guided **LabSpec** into the same IR 2.0 document / `ActiveWorkflowRef`; publishes `WorkflowRun` on completion.
- **Operator bind (D-W5):** dock installs `makeRegistryNodeExecutor()` — nodes with registry `operatorId` execute via `RSOperatorRegistry`; empty/unknown ids **typed-refuse** (`ir2.operator_unbound:`). Synthetic default remains only for hermetic D17 tests (no `setExecutor`).
- **Multi-input port→param (D-W6):** coordinator keys inbound artifacts by IR2 **target port name**; `applyIr2InputPortMapping` fills operator params by explicit port names (prefer names over order-only); unbound refusal still before mapping.
- `WorkflowDocument` alias advanced on IR2 surface (canvas / LabSpec lift / guided / `startRun`); Engine 2.0 untouched.
- **D15** classification Results are path-backed when products exist; provisional ids only as fallback.
- Prior mount slice retained: D14 dual / D15 studio / D17 canvas menus + publish helpers.

## Baseline SHA

- `origin/master`: `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb`
- Branch: `grok/unified-mission-workbench-d18`

## Architecture

MissionContext remains a value aggregate under `src/app/workbench/`. Main window owns session `m_mission`. Persistence is dual-write (sidecar primary on restore). Specialist UIs retained; IR2 dock owns its coordinator instance (no third scheduler). Node work uses existing `NodeExecutor` + `RSOperatorRegistry` + port→param helper.

## Authority / convergence decisions

See `DECISIONS.md` (D-W1–W6, D-W3b, D-M1–M5, D-I1–I4).

## Major deliverables

- Audit + MissionContext + mounts (prior)
- Project persist dual-write + IR2 run/LabSpec + classify path Results (prior)
- IR2 registry executor bind + WorkflowDocument IR2 call-site rename (prior)
- Multi-input IR2 port→param mapping + hermetic unit tests (this continuation)
- Tests: `test_mission_context`, `test_mission_e2e_scaffolding`, `test_ir2_port_param_mapping`

## D14–D17 integration findings

- D14/D15/D17 menu-reachable (prior).
- IR2 → PipelineRunCoordinator start path wired; LabSpec lift available from dock.
- Operator plane: registry bind on dock; unbound refuse; port-named multi-input mapping; synthetic only off dock default path.
- Dual `WorkflowDefinition` name: alias used on IR2 surface; Engine 2.0 struct rename still deferred.

## Compatibility

Additive. Classic I2I/I2M/classification windows kept. Governance serializer unchanged. Engine 2.0 headers untouched. Coordinator `inputArtifacts` key semantic changes from source node id → target port name (D17 synthetic/custom executors that ignored keys remain fine; mapping helper accepts legacy node-id keys via order-only zip).

## Tests

- Catch2: dual-write, path-product, IR2 run identity, registry bind policy, **port→param mapping + unbound refuse helper**.
- **Local compile/ctest not executed on the agent box** (`cmake`/`g++` absent). See EVIDENCE.md.

## Performance / resource evidence

N/A for value-type / mount / executor wiring; policy remains `-j2` / `CTEST_PARALLEL_LEVEL=1` when toolchain available.

## Review findings

See REVIEW_LOG.md. Prior R2/R7 cleared; no new P0. Mapping limitations documented (no schema rename; legacy zip fallback).

## Known limitations

- Unbound / not-linked operator ids refuse at run time (honest); they are **not** silently synthetic.
- Port names should match operator param ids; mapper does not rename via operator schema.
- Legacy source-node-id artifact keys use order-only zip (deterministic but not preferred).
- Full ExecutionPlane/TaskCenter path remains Engine 2.0 (D-W1) — IR2 does not replace it.
- Full IR 2.0 struct rename (`WorkflowDefinition` → `WorkflowDocument` across all D17 TUs) still deferred.
- Guided LabSpec **cards** UI optional for dock LabSpec load.
- Full GUI E2E on toolchain host still pending.

## Follow-ups

1. Optional: schema-driven port→param rename when IR2 port names diverge from operator param ids.
2. Dedicated IR 2.0 struct rename / namespace split PR (beyond alias call sites).
3. Optional Qgs custom-property mirror if XML+sidecar insufficient for some hosts.
4. Run `ctest -R 'mission|ir2_port'` on Qt6+cmake host; GUI smoke of multi-input + unbound Run.

## Evidence policy

Local evidence only; no online CI dependency (GOAL section 18). Draft PR — **do not merge** until toolchain evidence lands.
