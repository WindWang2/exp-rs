# EVIDENCE — workflow-pipeline-designer (D17)

Policy: every capability claim maps to a local command + exit code, or is
explicitly marked not-executed. No online CI.

## Environment

- Host: Linux 6.18.49-2-lts x64, 16 logical cores / 62 GB RAM.
  Policy: build `-j2` (never `nproc`), tests `-j1`, drop to `-j1` on
  RSS > 70 %. QT_QPA_PLATFORM=offscreen for all UI tests.
- **Toolchain hazard found**: `/home/kevin/.local/bin/cmake` is a broken
  Python shim shebanged to the ZCode AppImage — it silently launches the
  desktop app and produces no build output. All D17 commands therefore use
  `/usr/bin/cmake` and `ninja` directly. (Reported for host cleanup; not
  fixed here — outside repo scope.)

## Resource Log

Sampling every ~60 s during long builds: appended below as `# Resource Log`
entries (date, load, RSS%). No threshold breach unless noted.

## Commands (chronological)

1. `git fetch origin master` → clean.
2. `git worktree add ../exp-rs-workflow-pipeline-designer -b
   zcode/workflow-pipeline-designer origin/master` → exit 0, HEAD
   `007e70cff6` (BASELINE.md).
3. `.gitignore` whitelist entry added; `git check-ignore -v
   .planning/workflow-pipeline-designer/GOAL.md` → matches
   `!.planning/workflow-pipeline-designer/*.md` (tracked OK).
4. Phase-0 docs committed (GOAL/PLAN/DECISIONS/BASELINE/ADR-0162/…).
5. `/usr/bin/cmake --preset dev-default -DFETCHCONTENT_SOURCE_DIR_PYBIND11=
   /home/kevin/projects/rs-studio/main/build-dev/_deps/pybind11-src
   -DCMAKE_{C,CXX}_COMPILER_LAUNCHER=/usr/sbin/ccache` → **exit 0**
   ("Generating done (9.0s)").
