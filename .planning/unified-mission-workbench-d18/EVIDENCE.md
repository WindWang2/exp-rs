# EVIDENCE — D18

## Environment (2026-09-15 Asia/Shanghai UTC+8)

- Box: Linux agent workspace; `cmake` and `g++` **not installed**.
- Builds/tests: **not executed** on this box. Sources and CMake/test targets added for the Windows/CI toolchain used by the repo.
- Auth: `GH_TOKEN` from `/home/box/.config/gh-token` for push/PR only.

## Local commands that did run

```text
git rev-parse HEAD / origin/master
gh pr list / gh pr edit / git push
code search / header reads under src/app, src/workflow, tests
```

## Build evidence policy

Per GOAL section 18: no online CI dependency. When a machine with Qt6+cmake is available:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=2
export CTEST_PARALLEL_LEVEL=1
export QT_QPA_PLATFORM=offscreen
cmake --build <build> --target test_mission_context test_mission_e2e_scaffolding test_ir2_port_param_mapping sicnu_geo_rs -j2
ctest -R 'test_mission|test_ir2_port_param' -V
```

## Implementation landed (port→param mapping continuation)

- **D-W6 multi-input mapping** (`ir2_port_param_mapping.*`): `applyIr2InputPortMapping` — explicit IR2 target port names → operator params; `ir2_input_artifacts` port→path; primary `input` alias rules; legacy sourceNodeId order-only zip fallback
- **Coordinator** keys `inputArtifacts` by `EdgeFact.targetPortName` (prefer names over order-only)
- **Registry executor** uses the helper; unbound refusal still **before** mapping
- Tests: `test_ir2_port_param_mapping` (mapping + unbound refuse helper); `scenario3c` contract updated (not executed)

### Bound vs still synthetic / unbound

| Path | Behavior |
|------|----------|
| Dock Run + `node.operatorId` in `RSOperatorRegistry` | **Bound** — real `RSOperator::execute` with port→param map |
| Dock Run + empty/unknown `operatorId` | **Unbound refusal** (not synthetic; before mapping) |
| Coordinator with no `setExecutor` (D17 unit tests) | **Synthetic default** (hermetic) |
| Explicit `makeSyntheticNodeExecutor()` | **Synthetic** (tests only) |

### Mapping limitations (honest)

- Port names should match operator param ids; no schema-driven rename.
- Legacy node-id keys use deterministic order-only zip — prefer port names.
- cmake/ctest **not executed** on this box.

## Not executed (toolchain absent)

- `cmake` configure/build
- `ctest -R mission|ir2_port`
- GUI smoke of Run with multi-input / unbound refusal

## Commits on branch (after seed)

| SHA | Message |
|-----|---------|
| 733388cd | docs(d18): Phase A audit |
| 5e05bf25 | feat(d18): MissionContext value type |
| d7a678a2 | feat(d18): bind MissionContext into studios |
| a6b0cab5 | feat(d18): workbench:context mission summary |
| 862cfb09 | feat(d18): MissionContext sidecar save/load |
| 9a07209a | docs(d18): record PR #991 |
| e6c8affb | feat(d18): mount D14/D15/D17 + publish Result/workflow identity |
| a46941e5 | docs(d18): evidence, decisions, review log, PR body for mount slice |
| 3544f4ad | feat(d18): dual-write MissionContext on project save/open |
| c5773131 | feat(d18): IR2 PipelineRunCoordinator/LabSpec + path-backed classify Results |
| ec7175d6 | docs(d18): evidence, decisions, review log, PR body for persist/run slice |
| 59f0533e | docs(d18): fill evidence commit ledger SHAs for persist/run slice |
| e1700670 | docs(d18): note tip SHA 59f0533e in evidence ledger |
| a640a677 | feat(d18): bind IR2 dock to RSOperatorRegistry NodeExecutor |
| 9fbf0789 | docs(d18): evidence, decisions, review, PR body for operator-bind |
| a4d26573 | feat(d18): multi-input IR2 port→param mapping |
| 8ef925a9 | docs(d18): evidence, decisions, review, PR body for port-map |

## PR

- Draft: https://github.com/WindWang2/exp-rs/pull/991
- Not merged (per GOAL).
- Tip SHA after port-map docs: `8ef925a9` (includes docs commit).
