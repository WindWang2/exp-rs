# BASELINE — linked-visual-analytics-11

Phase 0 read-only audit, executed 2026-09-16 from the main checkout
(`C:\Users\wangj.KEVIN\projects\exp-rs`) BEFORE creating this worktree.

## Git / GitHub facts (fresh at start)

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  "fix: fail-closed fixes for review issues #994–#999 (#1000)".
  The prompt-generation snapshot (`ebcafb4d`) is stale; everything below is
  re-verified against `a5b11b7f`.
- Recent master log (20): a5b11b7f (#1000 fail-closed fixes), 1cea9892 merge
  #992 (d19 dataset foundry/benchmark), c5d4aafe #991 (D18 unified mission
  workbench — MissionContext + IR2 dock), 77e178ac ci fix #993, then d19
  commits (08264801..d3387fcb), ebcafb4d (#990), b91753ff merge classification
  change studio, f368b9fd merge workflow pipeline designer, 64418b72 merge
  geometric registration, e8c4bf43 D16 temporal phenology (#986).
- Remote branches (by recency): `zcode/execution-runtime-convergence-11`,
  `zcode/radiometric-spectral-workbench`, master.

## Open PRs at start

| PR | Branch | State | Changed-file overlap with this track |
|---|---|---|---|
| #1009 execution-11 | `zcode/execution-runtime-convergence-11` | open, UNSTABLE | NONE in primary scope (src/runtime/**, src/operators/framework/**, src/processing/framework/**, src/workflow/pipeline_run_coordinator.cpp, CHANGELOG.md, tests/test_*execution*). Shared-file cautions: CHANGELOG.md, tests/CMakeLists.txt, .gitignore → append-only only. |
| #1008 spectral d13 | `zcode/radiometric-spectral-workbench` | open, DIRTY | NONE in primary scope (src/app/widgets/{band_composite_palette,spectral_profile_widget}, src/core/radiometric_state|spectral_library, src/analysis/{atmospheric,hyperspectral}, src/processing/algorithms/{radiometric_calibration,spectral_indices,spectral_unmixing}, src/agent/spatial_tools/spectral_*). Shared-file cautions: src/app/CMakeLists.txt, tests/CMakeLists.txt → append-only only. |

PRs #991 (mission workbench) and #992 (dataset foundry) were MERGED into
master before this track started (c5d4aafe, 1cea9892). Their content is now
master fact; the former "read-only while #991 open" restriction on
main_window_* / mission_context* no longer applies — main_window edits are
still kept minimal (integration wiring only).

## Open issues at start (#1001–#1007, dedupe)

- #1001 io:clip srcCrsOverride misuse — io domain, not this track. OUT_OF_SCOPE.
- #1002 workflow registry executor fail-open — workflow domain. OUT_OF_SCOPE.
- #1003 dataset joinFeaturesBySampleId JSON-null — dataset domain. OUT_OF_SCOPE.
- #1004 dataset:qa scan_capped uniqueness — dataset/agent. OUT_OF_SCOPE.
- #1005 georef mapPickToLayerCrs silent wrong GCP — georeferencer domain.
  Related thematically (CRS transform failure semantics) but different
  component; OUT_OF_SCOPE, referenced as a design caution: our link
  controllers must fail CLOSED on CRS transform errors (leave target view
  unchanged + count the failure), never silently use untransformed geometry.
- #1006 PipelineRunCoordinator syntheticExecute default — workflow. OUT_OF_SCOPE.
- #1007 dataset:qa CRS audit — dataset. OUT_OF_SCOPE.
None of these are implemented by this track; none block it.

## ISSUES.md / CHANGELOG check

`ISSUES.md` is the old D3 lab-content backlog (T-1..T-3, S-1..S-2, H-1..H-3,
C-1..C-2 operator gaps). None of its items belong to linked visual analytics;
not treated as live backlog. CHANGELOG.md skimmed: no linked-view/VA hub work
in flight or shipped beyond the 10.0 VA platform entries.

## Code facts verified at `a5b11b7f` (the gap this track closes)

1. `src/app/shell/view_link_controller.{h,cpp}` — N-view linked extent over
   QgisDisplayManager (16 ms throttle, mApplying reentrancy guard,
   viewAboutToBeRemoved auto-detach, cross-CRS transformBoundingBox,
   per-view linked set + snap-on-link). **Instantiated NOWHERE in the shell**
   (only tests/test_view_link.cpp references it). No link groups, no
   viewport history/undo, no cursor sync.
2. `src/app/shell/rs_dual_viewport_sync_controller.{h,cpp}` — dual 1x2
   split-view sync (extentsChanged only; no cursor). Different surface;
   kept as-is.
3. `src/app/visualanalytics/` — va_data.h (typed bounded payloads),
   va_source.h (RsScanPool async + generation tokens), va_chart_widget
   (painter charts; emits rangeSelected/pointSelected/categorySelected; NO
   hover signal), va_workbench_panel (3 charts; histogram→scatter filter is
   PRIVATE client-side logic). `VaSelectionHub` is named in a comment
   (va_chart_widget.h) but **does not exist**.
4. `QgisDisplayManager` (ProjectContext-owned by value) emits only
   activeViewChanged / autoDisplayFailed / viewAdded / viewAboutToBeRemoved /
   viewRemoved. `setLayerVisible` emits NOTHING. DisplayLayerId authority =
   QgsMapLayer custom property "sicnu/displayLayerId"; cross-view identity of
   the same asset = AssetId (DisplayLayerSnapshot::assetId()).
5. Cursor seam: vendored QGIS `src/gui/qgsmapcanvas.h:1116`
   `void xyCoordinates(const QgsPointXY &p)`; only consumer today is
   main_window_connections.cpp:52 (status bar).
6. Command registry: CommandDefinition{id,title,desc,icon,shortcut,category,
   keywords,checkable,destructive,availability,checkedState,explain,handler};
   `workbench.visualAnalytics` exists (command_defs.cpp:317). **No `view.*`
   commands exist.** Contract test `test_command_contract_9.cpp` enforces
   help↔registry 1:1 coverage against `data/help/commands.json`
   ("command.<id>" scheme) — new commands MUST add help entries.
7. Tests: test_view_link.cpp (3 cases), test_visual_analytics.cpp (5 cases),
   test_dual_viewport_sync.cpp (9 cases). House pattern: per-file Catch2 main
   over heap QgsApplication, QT_QPA_PLATFORM=offscreen, controllers compiled
   directly into the test executable.
8. Build: preset `dev-default` → `build-dev` (Debug, ENABLE_TESTS=ON);
   configure recipe in `configure_build.cmd` (Ninja, Qt 6.8.0 msvc2022_64,
   vcpkg toolchain, prefix C:\deps\Qt\6.8.0\msvc2022_64 + qca + kc).
   Root `build.cmd` targets a DIFFERENT worktree
   (exp-rs-unified-help-diagnostics-6) — must NOT be used here; cmake is
   invoked explicitly in this worktree instead.
9. `.gitignore` ignores `.planning/*` with per-track md-only whitelists
   (~lines 243-254); this track adds its own 3-line whitelist (append-only).
10. docs/workbench/ does not exist yet; docs/ui-architecture.md §31 (VA
    platform) and §33 (N-view link) document the 10.0 state and list
    contracts-under-test.

## Skills confirmed present

.agents/skills/{codebase-design, code-review, diagnosing-bugs, domain-modeling,
ask-matt, implement-spec, implement}/SKILL.md all exist (loaded as needed;
goal-loop protocol run per the prompt's embedded contract).
