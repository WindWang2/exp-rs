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

Per GOAL §18: no online CI dependency. When a machine with Qt6+cmake is available:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=2
export CTEST_PARALLEL_LEVEL=1
export QT_QPA_PLATFORM=offscreen
cmake --build <build> --target test_mission_context test_mission_e2e_scaffolding sicnu_geo_rs -j2
ctest -R 'test_mission' -V
```

## Implementation landed (this continuation)

- Mission **dual-write** on project save/open: sidecar `.mission.json` + `sicnuMissionContext` XML (not inside DataProjectSerializer — D-M5)
- IR2 designer dock: **PipelineRunCoordinator** Run/Cancel + **LabSpec** load via `labspec_workflow_lift`; shared `ActiveWorkflowRef`; WorkflowRun publish on completion
- `WorkflowDocument` alias for IR 2.0 (full rename deferred — D-W3)
- Classification: path products via `classificationProductReady` / classic `requestLoadToMainMap` / artifact_paths reuse — less provisional Results
- Tests: XML dual-write + path-product upgrade + IR2 run identity contract (not executed)

## Not executed (toolchain absent)

- `cmake` configure/build
- `ctest -R mission`
- GUI smoke of new menu / Run / LabSpec actions

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
| (pending) | feat(d18): mission dual-write + IR2 PipelineRunCoordinator/LabSpec + classify path Results |

## PR

- Draft: https://github.com/WindWang2/exp-rs/pull/991
- Not merged (per GOAL).
