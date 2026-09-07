# ADR 0130: Unified Application Shell (Desktop Workbench UX 4.0, Milestone A)

- Status: Accepted (Desktop Workbench & Unified UX 4.0 goal)
- Context: the shell had accumulated fragmentation that users could see: the
  工作区治理 dock was created without its `WorkspaceService` (inert UI); the
  window title never reflected the open project; `BandCompositionRail` survived
  only as height-0 zombie chrome with a stale debug probe; `TaskCenterDock` and
  `MosaicPanel` were dead panels still compiled and tested; and the hidden
  menu bar carried literal duplicates of the ADR 0099 task-centric 遥感 menu.
- Decision:
  1. **Panel wiring is a shell contract**: a dock body that requires a service
     receives it at construction (`setupDataManagerPanel` injects
     `ProjectContext::workspaceService()`); the shell re-queries the panel
     after any store open/reopen (`refreshWorkspaceBrowser`). A source-scan
     test (`test_workspace_browser_wiring.cpp`) pins this.
  2. **Project context is visible**: the window title mirrors
     `[·]<project> — SICNU GEO RS` (a `*` dirty marker is prepended), driven
     by `QgsProject::isDirtyChanged` and the new/open/save lifecycle.
  3. **Dead UI is deleted, not hidden**: `TaskCenterDock`, `MosaicPanel`,
     `BandCompositionRail` and their tests/debug probes were removed after
     their unique capabilities were ported to living surfaces (pipeline
     grouping and error text live in `RsJobPanel`; band composition lives in
     the ribbon 地图 tab).
  4. **One capability, one menu action**: the 遥感 menu (ADR 0099) is the
     single menu entry point for product preprocessing/spectral/change/
     fusion/terrain; 栅格/分析 keep only their unique fine-grained entries
     (配准/镶嵌/波段, 时间序列分析, 分类). Ribbon remains the visible surface.
  5. **Ribbon collapse state persists** (`ribbon/collapsed`) alongside the
     dock-layout state.
- Consequences: no silent empty panels; project identity is always visible;
  the action registry shrinks; menus stop contradicting the ADR 0099 surface;
  ~890 lines of dead panel/chrome code plus ~330 lines of their tests are
  gone.
