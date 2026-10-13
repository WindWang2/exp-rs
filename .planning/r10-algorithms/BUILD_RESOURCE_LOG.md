# BUILD_RESOURCE_LOG — R10-A

Host: Windows 10.0.26200, 16 logical cores / 31.2 GB RAM, MSVC 2022 (14.38), disk C: 519 GB free at start.

| event | time | parallel | RAM free before | notes |
|---|---|---|---|---|
| configure build-dev | 2026-10-13 ~12:20 | — | 13.7 GB | 127.8 s configure + 38.9 s generate; vcpkg manifest install fully cache-hit (~800 MB binary cache), no package rebuild |
| first build attempt | ~12:40 | -j2 | — | ninja abort: resources/icons symlink checked out as 33-byte file (Windows); fixed via directory junction to docs/design/ui/svg-icons/icons (LOCAL BUILD FIX ONLY — never committed; r9 worktrees use the same workaround) |
| full build (restart) | 12:4x→ | -j2 (CMAKE_BUILD_PARALLEL_LEVEL=2) | 13.7 GB at start | 9096 CXX objects; progress checkpoints: 716 @ ~13:00, 2683 @ ~14:10 |
| tests | TBD | CTEST_PARALLEL_LEVEL=1 | TBD | per CLAUDE.md |

Rules honored: total -j4 ceiling machine-wide (we self-cap at -j2); no -j$(nproc);
only this worktree compiles during the window (subagents were read/test-write only).
