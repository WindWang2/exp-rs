# EVIDENCE — linked-visual-analytics-11

Append-only log of executed commands + outcomes. (Claims come only from here.)

## Phase 0 (main checkout, read-only)
- git fetch origin --prune → OK; origin/master=a5b11b7f10fa010c1c060864fb427d777ba9a4aa
- git log -20 --oneline origin/master; git branch -r → recorded in BASELINE.md
- gh pr list (2 open: #1009, #1008); gh pr diff --name-only both → recorded in PARALLEL_OWNERSHIP.md
- gh issue list (7 open: #1001–#1007) → dedupe in BASELINE.md
- ISSUES.md / CHANGELOG.md / docs/agents/goal-template.md read → no live VA items
- Subagent #1 (Explore, read-only) architecture audit → facts in BASELINE/CURRENT_ARCHITECTURE
- git worktree add ../exp-rs-linked-visual-analytics-11 -b zcode/linked-visual-analytics-11 origin/master → OK, HEAD=a5b11b7f

## Phase 6a — build (worktree-local, fresh configure)
- configure: cmake -S . -B build-dev -G Ninja (Debug, ENABLE_TESTS=ON,
  FETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src, Qt 6.8.0 msvc2022_64 +
  vcpkg toolchain; winflexbison on PATH) → OK (1392.8 s configure).
  First attempt failed: FetchContent tried to clone Catch2 from GitHub
  (offline host) — resolved with the local v3.7.1 source dir, NOT by editing
  CMakeLists.
- build: cmake --build build-dev --target test_view_link test_visual_analytics
  with CMAKE_BUILD_PARALLEL_LEVEL=2. Fresh build-dev → full dep graph (3099
  steps). Resource samples: freeRAM 1.2 GB, cl.exe RSS ≈ 126–474 MB (2
  workers) — within budget, -j2 held; drop to -j1 only on OOM (none seen).
