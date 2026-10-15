# Baseline: R13-J Security and Concurrency Hardening

## Environment & Git State
- **Base Commit**: `f72f0c11731361488a384a085797aae64efaf7ee` (`origin/master` HEAD at 2026-10-15)
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r13-j-security`
- **Branch**: `hardening/r13-j-security-concurrency-fixes`
- **Build Directory**: `build-r13-j`
- **Compiler / Toolchain**: GCC 13+ / Clang 18, Ninja, C++20, Qt 6.4+, GDAL 3.8+
- **Lock File**: `/tmp/exp-rs-agent-locks/global-build.lock` (`-j2` compilation, `-j1` testing)

## Upstream State & Cross-Branch Awareness
- Latest Merges on Master:
  - `#1501`: `fix/all-open-issues` (systematic root cause resolution for open issues #1447 ~ #1495)
  - `#1503`: `hardening/r13-g-cartography-mapspec`
- Open PRs:
  - `#1504`: `hardening/r12-b-geodata` (tracked in parallel, non-overlapping with concurrency & runtime fixes)
- Local Worktree State:
  - Worktree created strictly isolated from other tracks.
