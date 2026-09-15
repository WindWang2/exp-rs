# EVIDENCE — D18

## Environment (2026-09-15 UTC)

- Box: Linux agent workspace; `cmake` and `g++` **not installed**.
- Builds/tests: **not executed** on this box. Sources and CMake/test targets added for the Windows/CI toolchain used by the repo.
- Auth: `GH_TOKEN` from `/home/box/.config/gh-token` for push/PR only.

## Local commands that did run

```text
git rev-parse HEAD / origin/master
gh pr list --state merged/open
code search / header reads under src/workflow, src/agent/harness, src/app/workbench, src/app/pipeline
```

## Build evidence policy

Per GOAL §18: no online CI dependency. When a machine with Qt6+cmake is available, preferred targeted commands:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=2
export CTEST_PARALLEL_LEVEL=1
export QT_QPA_PLATFORM=offscreen
# configure per repo presets, then:
cmake --build <build> --target test_mission_context -j2
ctest -R test_mission_context -V
```

## Implementation landed (this run)

- Audit docs under `.planning/unified-mission-workbench-d18/`
- `src/app/workbench/mission_context.{h,cpp}`
- Studio hooks: ClassificationStudioWidget mission refs; TemporalWorkbenchPanel::exportTemporalContext
- Tests: `tests/test_mission_context.cpp`, `tests/test_mission_e2e_scaffolding.cpp`
- CMake: app source + two Catch2 targets

## Not executed (toolchain absent)

- `cmake` configure/build
- `ctest -R mission`
