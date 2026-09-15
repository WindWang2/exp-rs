# AUDIT_D14_D17 — Cross-track integration after the merge wave

Baseline master: `ebcafb4d`. Tracks: D14 #987, D15 #989, D16 #986, D17 #988.

## Mounting matrix (main shell)

| Surface | Implementation | In `src/app/CMakeLists.txt`? | Opened from main window? | Shares MissionContext? |
|---------|----------------|------------------------------|--------------------------|------------------------|
| Classic georef I2I / I2M | `QgsGeoreferencerMainWindow` / `QgsGeorefImageToMapWindow` | yes (georeferencer/) | **yes** (`openGeoreferencer` / menus) | no — path-based `requestLoadToMainMap` |
| D14 dual-window | `rs::app::GeorefDualWindow` | **yes** (`workbench/georef_dual_window.cpp`) | **no call site** found in `main_window*` | no |
| Classic classification | `QgsClassificationMainWindow` | yes (ifdef SICNU_HAS_CLASSIFY) | **yes** | partial — DataManager inject; path reload |
| D15 studio widget | `ClassificationStudioWidget` | **yes** | **no call site** in main_window* | no |
| Temporal panel (pre-D16 + dock) | `TemporalWorkbenchPanel` | yes | **yes** (`showTemporalWorkbench`) | no dedicated mission bind |
| D16 timeline widget | `TemporalTimelineWidget` | **not listed** in app CMake | not mounted | n/a |
| Legacy pipeline dock | `sicnu::workflow::gui::PipelineEditorDock` | yes (`app/workflow/*`) | **yes** (docks) | Engine 2.0 only |
| D17 canvas / guided | `src/app/pipeline/*` | **NO** (test-only link) | not in production binary | IR 2.0 only in tests |
| LabSpec guided (legacy) | `::GuidedWorkflowWidget` (widgets/) | yes | **yes** (docks) | LabSpec cards |

## Identity / publish gaps

- D14 `rectificationFinished(path, rmse)` and georef `requestLoadToMainMap(path)` re-import by **filesystem path**, not `Result`/`AssetId` publish into workspace governance.
- D15 studio binds `QgsRasterLayer*` directly — live pointer, not `WorkbenchObjectRef`.
- D16 timeline state is widget-local (ISO dates / scrubber index); Agent cannot inspect via a mission-level temporal context yet (`workbench:context` has `hasTemporal` fact but not acquisition/AOI ids).
- D17 `PipelineRunCoordinator` run ids are not pushed into `SelectionContext::selectedWorkflowRunIds` from the shell (that list exists for Workbench 10 object identity).

## Duplicate / parallel stacks (candidates — do not blind-delete)

| Area | A | B | Notes |
|------|---|---|-------|
| Workflow document | Engine 2.0 `WorkflowDefinition` | IR 2.0 `WorkflowDefinition` | Same C++ name; see AUDIT_WORKFLOW |
| Pipeline canvas | `app/workflow/PipelineCanvasWidget` | `app/pipeline/PipelineCanvasWidget` | Different namespaces; both "pipeline" |
| Guided workflow UI | `widgets/GuidedWorkflowWidget` | `pipeline/GuidedWorkflowWidget` | D17 DECISIONS D1 |
| Run coordinator | `WorkflowRunCoordinator` | `PipelineRunCoordinator` | Production vs designer-local |
| Georef UI | Qgs georeferencer shell | `GeorefDualWindow` | Dual-window not menu-wired |
| Classification UI | QgsClassificationMainWindow | ClassificationStudioWidget | Studio not menu-wired |
| Temporal UI | TemporalWorkbenchPanel | TemporalTimelineWidget | Timeline not in app CMake |

## Post-merge CMake / registration risks

- D17 production UI sources absent from app target → "compiled in tests but not production" (GOAL §15).
- Two pipeline scenes increase risk of wrong include / wrong WorkflowDefinition.
- `.planning` whitelist for D18 already present in `.gitignore` (`!.planning/unified-mission-workbench-d18/**`).

## Integration priorities for D18 (ordered)

1. Introduce serializable **MissionContext** (typed refs, no QObject pointers).
2. Document workflow authority graph; plan IR 2.0 rename / alias.
3. Wire MissionContext into SelectionContext projection + `workbench:context` summary.
4. Mount or bridge D14 dual-window / D15 studio / D16 timeline / D17 pipeline onto shared mission refs (keep specialist UIs).
5. Publish studio outputs as Result/Asset identities without path re-import.
6. Agent ↔ UI same workflow document fingerprint.
7. Mission save/restore + E2E scenarios.
