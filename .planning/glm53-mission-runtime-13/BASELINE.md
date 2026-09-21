# BASELINE — glm53-mission-runtime-13 (Mission Workbench & Agent Mission Runtime 13.0)

## Live trunk at Phase 0

- `git fetch origin --prune` executed in the read-only main checkout before any work.
- `origin/master` = **79adfe78a16b9419eef180cf9e6e5739658621a2** (2026-09-20 21:39:35 +0800),
  "Merge pull request #1134 from WindWang2/agent/flash-plugin-sdk-12".
- Worktree created from that exact ref via `scripts/dev/new_worktree.py`:
  `/home/kevin/project/exp-rs-worktrees/mission-runtime-13`, branch
  `agent/glm53-mission-runtime-13`. `git merge-base HEAD origin/master` == the ref above.
- No stale `agent/glm53-mission-runtime-13` branch existed; no branch was reused.

## Open PRs at Phase 0 (3) — none in the mission domain

| PR | Title | Overlap with this track |
| --- | --- | --- |
| #1137 | geospatial maintenance 13.0 (retry admission, Stat TTL parity, UTF-8 orphan deletion) | none — remote/geospatial maintenance |
| #1136 | Classroom-safety 13.0 (offline labs registry, lab identity, CSV safety) | `tests/CMakeLists.txt` only (append-only) |
| #1135 | Temporal Phenology & Irregular Time-Series Analytics 12.0 | `tests/CMakeLists.txt` only (append-only) |

`scripts/dev/overlap_scan.py --from-HEAD` over the planned paths
(`src/app/workbench`, `src/agent/spatial_tools`, `src/app/main_window_workbench.cpp`,
`pi/exp-rs-spatial.ts`, `tests/CMakeLists.txt`) reports no open PR touching any mission
source file. The three open PRs are geospatial/labs/temporal domains — explicitly
non-goals here.

## Open issues at Phase 0

`gh issue list --state open` → **0 issues**.

## Direct predecessor: PR #1121 (Workbench 12.0, merged)

Body + diff + review threads read. It landed the mission *task space value layer*:
`mission_stage.*` (state machine, timeline, reconciliation), `mission_projection.*`
(one projection + surface identity registry), `mission_timeline_model.*` (paged model),
`mission_timeline_store.*` (sidecar persistence) and four gates
(`test_mission_stage`, `test_mission_surface_parity`, `test_mission_scale_benchmark`,
`test_mission_workbench_e2e`). Its own stated known limitations define this Track:

1. "The `mission:*` SpatialTool objects and their shell wiring are not in this PR."
2. "The timeline is not yet embedded into `MissionContext::metadata`; the sidecar is
   the transport for now."
3. `mission_timeline_store.cpp` compiles into no target and `test_mission_workbench_e2e.cpp`
   is registered nowhere (verified again at this baseline — both statements still hold).

## Remote `agent/*` / `track/*` / `fix/*` branch residue

All remaining remote branches are merged residue or other live tracks
(`stale_branches.py` evidence recorded in DEDUP.md). No mission-domain branch is in flight.

## Environment facts (build)

- cmake 3.30.5 at `/tmp/cmake-3.30.5-linux-x86_64/bin/cmake`; ninja; g++; Qt 6.11.2
  (`/usr/lib/cmake/Qt6`); jsoncpp (`/usr/lib/libjsoncpp.a`, headers `/usr/include/json`);
  GDAL/PROJ/GEOS SDK at `/home/kevin/pwb-sdks/root/usr`.
- No vendored-QGIS prebuilt tree is usable as a CMake dependency (the repo builds its
  QGIS subset from `src/core` as part of one CMake project); a full app build is out of
  proportion here, exactly as in #1121 (DECISIONS.md D1).
- Catch2 v3 source tree available locally at
  `/home/kevin/project/exp-rs-worktrees/geo-maintenance-13/build-dev/_deps/catch2-src`
  (used via `FETCHCONTENT_SOURCE_DIR_CATCH2`, no network).
- Verification strategy: an out-of-tree Catch2 harness that compiles the *real* repo
  sources of the mission domain + mission tool objects (Qt Core + Xml + jsoncpp only —
  the SpatialTool base class and the mission value model are QGIS-free by design).
