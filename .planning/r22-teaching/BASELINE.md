# R22-06 teaching track — BASELINE

- date: 2026-10-25T13:19Z
- repo: WindWang2/exp-rs (viewer permission: ADMIN)
- base: origin/master = 911e0b17a3533ca0d79c1b68b000d6052c4c3c61 (matches prompt snapshot)
- branch: agent/r22-teaching-20261025T131928
- worktree: C:/Users/wangj.KEVIN/projects/exp-rs-r22-teaching-20261025T131928
- main worktree was clean of tracked modifications; only untracked scratch files.

## Commands run (startup inventory)

```
git fetch origin master --prune        -> 911e0b17a (no new commits since snapshot)
gh repo view --json defaultBranchRef,viewerPermission -> master / ADMIN
gh pr list --state open                -> #1673 #1674 #1675 #1676 #1677 (all R21, none merged)
gh issue list --state open             -> R17 triage batch #1678-#1707 open
gh issue view 1694 1689 1688 1678 1558 -> bodies read in full
git worktree list                      -> only main worktree before ours
```

## Open PRs (collision candidates)

| PR | head | files overlapping this track |
|----|------|------------------------------|
| #1673 r21-B data/model-runtime | none in teaching/lab/verify |
| #1674 r21-E teaching-verification | src/agent/output_verifier.*, src/experiment_studio/live/studio_live.cpp, src/faultlab/*, src/verify/CMakeLists.txt, src/verify_adapters/lab_verify_composer.*, tests/CMakeLists.txt |
| #1675 r21-C pi/mcp | none |
| #1676 r21-A science | none |
| #1677 r21-D qt/qgis | none |

=> This track must NOT reimplement #1674's verifier production-wiring; treat its
files as read-only (see OWNERSHIP.md) and record dependencies in
CROSS_TRACK_FINDINGS.md. tests/CMakeLists.txt is an append-only shared registry.

## Build environment (host facts)

- Windows 10 (win32), Git Bash shell.
- Toolchain NOT on default PATH; manual MSVC env via `C:/Users/wangj.KEVIN/projects/exp-rs/.msvc_env.sh`
  (vcvars64.bat blocked in sandbox). Effective tools:
  - cl: C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.38.33130
  - cmake: C:/Qt/Tools/CMake_64/bin, ninja: C:/Qt/Tools/Ninja
  - Qt 6.8.0 msvc2022_64 at C:/deps/Qt/6.8.0/msvc2022_64 (CMAKE_PREFIX_PATH also qca/kc installs)
- Reference build dir of main worktree: build-dev (Ninja, Debug, ENABLE_TESTS=ON).
- This track uses its own build dir `build-r22-teaching` inside the worktree; -j2..-j4 hard cap.
- Shared build lock path (mandated): /tmp/exp-rs-r21-build.lock (flock not guaranteed on Git Bash —
  fallback: mkdir-based lock + retry; see LOOP_LEDGER).
