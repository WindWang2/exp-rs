# EVIDENCE — D18

## Environment (2026-09-15 UTC / Asia/Shanghai UTC+8)

- Box: Linux agent workspace; `cmake` and `g++` **not installed**.
- Builds/tests: **not executed** on this box. Sources and CMake/test targets added for the Windows/CI toolchain used by the repo.
- Auth: `GH_TOKEN` from `/home/box/.config/gh-token` for push/PR only.

## Local commands that did run

```text
git rev-parse HEAD / origin/master
gh pr list / gh pr edit
code search / header reads under src/app, src/workflow, tests
```

## Build evidence policy

Per GOAL §18: no online CI dependency. When a machine with Qt6+cmake is available:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=2
export CTEST_PARALLEL_LEVEL=1
export QT_QPA_PLATFORM=offscreen
cmake --build <build> --target test_mission_context test_mission_e2e_scaffolding sicnu_geo_rs -j2
ctest -R 'test_mission' -V
```

## Implementation landed (this continuation)

- Mission publish helpers: `publishMissionObject` / `publishMissionResultFromPath` / `publishMissionLayer` / `setMissionActiveWorkflow`
- D14 `GeorefDualWindow` menu + workbench + command; `rectificationFinished` → mission Result + optional map load
- D15 `ClassificationStudioWidget` menu + workbench + command; selection → mission input ref; classificationRequested → Result id
- D17 IR 2.0 production mount: `Ir2PipelineDesignerDock` + canvas sources + `workflow_ir_v2.cpp` in `sicnu_geo_rs` CMake (no Engine 2.0 header co-include)
- Shared workflow identity: dock emits `ActiveWorkflowRef` into `m_mission`; `workbench:context` prefers session mission
- E2E scenarios 1–5 strengthened (contract tests); honest not-executed without cmake/g++

## Not executed (toolchain absent)

- `cmake` configure/build
- `ctest -R mission`
- GUI smoke of new menu actions

## Commits on branch (after seed)

| SHA | Message |
|-----|---------|
| 733388cd | docs(d18): Phase A audit |
| 5e05bf25 | feat(d18): MissionContext value type |
| d7a678a2 | feat(d18): bind MissionContext into studios |
| a6b0cab5 | feat(d18): workbench:context mission summary |
| 862cfb09 | feat(d18): MissionContext sidecar save/load |
| 9a07209a | docs(d18): record PR #991 |
| (pending) | feat(d18): mount D14/D15/D17 + publish Result/workflow identity |

## PR

- Draft: https://github.com/WindWang2/exp-rs/pull/991
- Not merged (per GOAL).
