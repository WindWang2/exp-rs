# PR / Branch Collision Matrix (R28-T04)

Fetch: 2026-11-04T21:03:52Z. Open PRs: **1**. Remote branches with agent prefixes: r23–r28 families (all merged or abandoned; see below).

| Branch / PR | Status | Relevance / conflict risk |
|---|---|---|
| **PR #1813** `fix/ui-review-2026-11-05` @ `4b27e1c089` | **OPEN** (2026-11-04) | Workbench UI polish: shared tab group, palettes, uiScale, copilot opt-in, catalog pagination, pipeline editor undo/redo, QSS/i18n. Touches `src/app/**` broadly but its `cartography_dock.cpp`/`experiment_studio_dock.cpp` hunks are layout-only. My F7/F12/F14/F18 + atlas work targets different logic. **Overlap files to re-diff before merge**: `src/app/cartography/cartography_dock.cpp`, `src/app/experiment_studio/experiment_studio_dock.*`, `src/app/shell/task_panel_host.cpp`, `resources/translations/sicnu_zh_CN.ts`, `tests/CMakeLists.txt`, `src/app/design_tokens.h`-adjacent QSS. |
| `agent/r28-t01-*` @ `949b1afa` | local WT exists, no open PR | Science track — disjoint paths. |
| `agent/r28-t02-*` @ `50cc3255c5` | local WT, no commits pushed yet | Data/IO runtime — owns `src/geospatial/**` (my read-only). ImportPreflight/ImportSession surface (WP03) consumes their API read-only. |
| `agent/r28-t03-*` @ `07eb4b47` | local WT, no open PR | Harness/SDK — owns `src/app/agent_ops/**`. |
| `agent/r27-*` (5) | merged via #1807–#1811 | all in BASE_SHA |
| `fix/swarm-review-2026-11-04` | merged via #1812 | in BASE_SHA |
| `agent/r23/r25/r26-*` | merged (#1715–#1731) | in BASE_SHA |

Conclusion: the only live collision surface is PR #1813. Plan: implement on disjoint logic; before PR, re-fetch #1813 and, if still unmerged, rebase onto latest master only (never onto #1813); resolve textual conflicts in shared files hunk-by-hunk.
