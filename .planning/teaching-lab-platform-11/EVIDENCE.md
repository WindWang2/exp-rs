# EVIDENCE — append-only log

Format: `UTC | phase | command | exit | artifact/notes`

## Phase 0
- 2026-09-16 | P0 | git fetch origin --prune; git rev-parse origin/master | 0 | a5b11b7f10fa010c1c060864fb427d777ba9a4aa
- 2026-09-16 | P0 | gh pr list / gh issue list / gh pr diff 1008,1009 --name-only | 0 | see BASELINE.md, PARALLEL_OWNERSHIP.md
- 2026-09-16 | P0 | worktree created: ../exp-rs-teaching-lab-platform-11 @ origin/master | 0 | branch zcode/teaching-lab-platform-11

## Resource log
- Build host: Windows (win32, Git Bash). CMAKE_BUILD_PARALLEL_LEVEL=2, CTEST_PARALLEL_LEVEL=1, ninja -j2. CPU/RSS sampling: see PERFORMANCE.md.
