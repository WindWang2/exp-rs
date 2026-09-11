# Desktop UI Architecture — SICNU GEO RS Workbench

Describes the desktop shell's information architecture, panel ownership, the
schema-driven form contract, the task/result UX contract, the design-token
rule, and the extension rules new features must follow. Living document —
update with the code that proves each claim.

## 1. Information architecture

```text
Top:    Ribbon (tabs: 工程 编辑 矢量编辑 地图 数据 预处理 增强 分析 分类 制图 任务 流程)
        Window title = "<project-file> — SICNU GEO RS 遥感分析平台" (dirty marker "*")
Left:   数据管理 (Data assets) | 工作区治理 (Results & governance) | 视图图层 | 文件浏览
Center: Map canvas / Layout studio / primary work areas (classification, OBIA, georeferencer)
Right:  处理工具箱, inspector docks (识别/光谱/拉伸), 任务 panel, 流程 editor, AI Copilot
Bottom: 任务中心 (RsJobPanel) | 系统日志
```

The visible surface is the **Ribbon**; the "menu bar" is a detached, hidden
QMenuBar used purely as the QAction/shortcut host. Capabilities with multiple
entry points (menu, ribbon tab, toolbar) share **one handler slot** — a single
code path per capability, never two divergent implementations.

### Concept model

| Concept | Meaning | Backend owner | Surface |
|---|---|---|---|
| Data | registered inputs, reusable datasets/collections | `DataManager` (project) | 数据管理 panel |
| Results | governed outputs with state/quality/provenance | `WorkspaceService` / `GovernanceStore` | 工作区治理 panel |
| Layers | current map presentation (visibility/order/style) | QGIS layer tree via `ActiveViewHost` / `QgisDisplayManager` | 视图图层 dock |
| History | runs, experiments, audit | `WorkspaceService` | 工作区治理 panel + RsJobPanel detail |
| Tasks | execution lifecycle | `TaskCenter` (sole owner) | RsJobPanel + TaskPanelHost |

Users never need the dual-plane detail: *Data* answers "what can I feed a
tool", *Results* answers "what did I produce and can I trust it", *Layers*
answers "what is on the map right now".

## 2. Panel ownership rules

- Every dock has a stable `objectName` and 窗口-menu toggle; layout restores
  via `saveState`/`restoreState` gated by `mainwindow/shellLayoutVersion`;
  ribbon collapse state persists under `ribbon/collapsed`.
- A panel never reaches into another panel's widgets. Cross-panel actions go
  through the owning service or a shell slot (`loadRasterLayer`,
  `ActiveViewHost::*`, `TaskCenter::*`).
- The **sole product task list is `RsJobPanel`** (bottom dock). It is a
  read-only projection of `TaskCenter`: it never owns execution state, and
  cancel/pause/retry route through TaskCenter methods. Pipeline steps run
  through `TaskCenter::submitPipeline` appear grouped under their parent task
  row.
- The right-side `TaskPanelHost` is the single-tool run surface (schema form
  + run/stop + result); it also projects TaskCenter state via
  `WorkflowSessionController`.
- `QgsClassificationMainWindow`, the OBIA window and the georeferencer are
  interactive work areas; their final compute is TaskCenter-tracked (see §4).

## 3. Schema-driven Operator UI (form contract)

`SchemaFormBuilder` (src/app/shell/schema_form_builder.cpp) converts an
authoritative schema into a validated parameter form:

- **Input**: `RSOperator::schema()` JSON, or `AlgorithmDescriptor::toInputSchema()`
  (ports carry `x-ui-type` hints: raster/vector/table/crs/bbox).
- **Editor mapping**: `x-ui-type` / `x-ui-widget` / JSON-Schema type+enum →
  field kinds, including asset (governed-asset combo, stable ids),
  model (ModelCatalog names), CRS (shared `CrsSelector`), color, JSON, arrays.
- **Defaults**: schema `default` is the single source of truth. The form never
  invents defaults; dialogs must not hardcode values that exist in the schema
  (e.g. rs:pca's `numComponents` default 0 = all bands).
- **Validation**: `validate()` checks required fields, numeric ranges,
  `minItems`, color syntax and JSON syntax; errors mark fields inline
  (`errorState` property + QSS) and populate the `rsSchemaValidation` summary
  line. `validationChanged(bool)` lets hosts gate the Run button.
- **Advanced discipline**: `x-ui-advanced` fields land in a checkable
  (collapsed) 高级 section — reachable, never invisible.
- **Accessibility**: every control carries `accessibleName` from its schema
  label; tab order follows schema order (advanced last).

Hosts embed the builder (TaskPanelHost) or generate forms directly. A dialog
that hand-rolls a form must either (a) reuse the shared widgets
(`RasterLayerCombo`, `BandRoleCombo`, `CrsSelector`) with defaults pulled from
the operator schema, or (b) justify why no schema-driven form fits.

## 4. Execution seam & thin-client law

Authoritative path: `UI → TaskCenter → JobEngine → RSOperator → kernel`.

- Dialogs submit operators through `RasterProcessingDialogBase::runOperatorTask`.
  Inline raster kernels in dialogs are banned — the 21 processing dialogs
  listed in `tests/test_ui_task_center_contract.cpp` are source-scanned (no
  `runGdalTask(`, no pixel I/O; read-only `GDALOpen` metadata probes are
  allowed). New dialog files must be added to that list.
- Kernel promotion pattern: file-level orchestration lives in
  `src/processing/algorithms/` (e.g. `band_tools.cpp`), wrapped by thin JSON
  operators in `src/operators/`. GUI keeps only parameter collection.
- Documented exceptions (TaskCenter-tracked, deliberate):
  - `processing:<algId>` — QGIS provider algorithms via the toolbox dialog.
  - `module:classify:*` — the classification lab's interactive
    train/preview/postprocess/CV flows around `RsClassificationPipeline`;
    headless parity exists via `rs:supervised_classification` /
    `rs:kmeans_classification` / `rs:post_classification_change`.
  - `module:georef:*` — session-owned interactive registration (SIFT match,
    template match, warp).
- Batch runs surface as one TaskCenter job with a custom executor; `rs:` ids
  inside batch resolve through `RSOperatorRegistry` (never the
  `AtomicAlgorithmRegistry` adapter directly).

## 5. Task / result UX contract

- Status vocabulary: the nine `TaskStatus` states render once, from
  `SicnuUi::Tokens::status*` colors (see §6), in RsJobPanel (and the
  georeferencer task list); TaskPanelHost delegates result status to
  `RsResultSummary`.
- Cancellation: Run morphs to Stop in TaskPanelHost; RsJobPanel offers
  Stop/取消 with confirmation and renders `Cancelling` truthfully.
- Results: one shared renderer `RsResultSummary`
  (src/app/widgets/rs_result_summary.cpp) shows status/context, key metrics,
  warnings, output artifacts (double-click = open on map) and collapsible raw
  JSON. It is embedded in TaskPanelHost and the RsJobPanel 结果 tab.
- Dialog result summaries: dialogs with domain-specific, localized summaries
  (QA-mask statistics, change metrics, transition matrix) keep rendering them
  from the ADR 0108 `onResult` seam — the shared view is for hosts that
  otherwise only show raw JSON. New dialogs should not hand-roll a generic
  "success" presentation when `RsResultSummary` fits.

## 6. Design tokens

`src/app/design_tokens.h` (`SicnuUi::Tokens`) is the single C++ owner of
semantic colors (both themes), task-status colors, spacing scale, icon sizes
and type sizes. `themeIsDark()` is the single theme probe.

- The token values mirror the QSS header comments in `resources/styles.qss`
  and `resources/styles-dark.qss`; `tests/test_theme_selector_parity.cpp`
  fails when they drift.
- New `src/app` code must not introduce local `QColor` literals for these
  roles.
- **Exception**: the pipeline editor (`src/app/workflow/`) renders a
  deliberately dark slate graphics scene in both themes and keeps its own
  vibrant badge palette tuned for that canvas. Its colors must not leak into
  panel/table code.

## 7. Accessibility / keyboard

- Shortcuts live on QActions in the hidden action-host menubar; the same
  binding must not be claimed by two actions in overlapping scope
  (`tests/test_shortcut_conflicts.cpp` guards the menus file).
- Shared controls carry accessible names; schema-form fields take theirs from
  schema labels.
- High-DPI: PassThrough rounding policy is set in `main.cpp`; dialogs expose
  responsive `minimumSizeHint` (tested); icon sizes come from
  `SicnuUi::Tokens::kIcon*`.

## 8. Migration notes (4.0)

- Removed: `TaskCenterDock` and `MosaicPanel` panels (dead since the product
  shell converged on RsJobPanel and the mosaic dialog; capabilities live in
  任务中心 and 栅格 > 预处理/镶嵌). Their tests were removed with them.
- Removed: `BandCompositionRail` zombie chrome (height-0 since the ribbon's
  地图 tab absorbed band composition). The live band UI is the ribbon tab.
- Menu dedup: product-level preprocessing (辐射定标/QA 掩膜/应用掩膜/大气校正/
  正射纠正) is reachable once under 遥感 > 产品与预处理; 分析 keeps only its
  unique entries (时间序列分析, 分类). Ribbon remains the visible surface.
- `rs:band_ratio`, `rs:extract_bands`, `rs:contrast_stretch`,
  `rs:image_enhancement`, `otb:bundle_to_perfect_sensor`, `gdal:pansharpen`
  are new operators (kernels promoted from dialogs; numerics preserved — IHS
  additionally masks NoData to NaN, aligned with the enhancement panel's
  #380 semantics).
- Window title now tracks project identity/dirty state; ribbon collapse state
  persists across restarts.

## 9. Extension rules (adding a feature)

1. New capability → implement the kernel in `src/processing/algorithms/`,
   expose an `rs:` operator with schema + metadata + estimate, register it
   (`rs_operators_init.cpp`), then add UI. No processing loops in UI, ever.
2. New parameter surface → SchemaFormBuilder from the operator schema.
3. New panel → own a single backend service; objectName + persistence
   contract; no cross-panel widget reaches.
4. New status/color → add to `design_tokens.h` + both QSS files in one change.
5. New result shape → extend `RsResultSummary` (it degrades to raw JSON).

---

# Part II — Professional Workbench 5.0

*(5.0 additions on top of the 4.0 principles above; every claim mirrors code
landed in this track.)*

## 10. WorkbenchHost — one workspace lifecycle (ADR 0134)

`src/app/workbench/` introduces three shell-level authorities:

- **`WorkbenchHost` / `IWorkbench`** — every professional workspace (map,
  layout, classification, georef I2I/I2M, OBIA, workflow) registers as a
  *workbench*: stable id, title, activate/deactivate, dirty query, close
  semantics, optional save/restore. Interactive sessions keep their own
  windows (`WorkbenchFeature::ExternalWindow`); the shell never forces them
  into tabs. `map` is always registered; a checkable 工作区 section in the
  窗口 menu switches benches; `activeWorkbenchChanged` drives the rest.
- **`SelectionContext`** — the single projection of "what is the user
  operating on": active layer + layer-tree selection (QGIS authoritative),
  Data Manager asset selection, governance entity selection, active
  workbench id. Changes broadcast coalesced (≤150 ms). Pure availability
  rules (`ContextRules`) map snapshots to capability groups (raster/vector/
  SAR/edit/result/asset) and to human unavailability reasons — no panel
  reads another panel, ever.
- **`CommandRegistry`** — one `CommandDefinition` per capability: single
  handler (forwards to the existing window slot/service), one availability
  predicate, one canonical shortcut (duplicate id/shortcut registration is
  rejected — contract-tested). Surfaces *project* commands:
  `CommandRegistry::action(id)` for menu-host QActions, ribbon
  `addCommandButton(id)` and the layer-tree context menu produce enabled-
  and reason-following projections. Nothing executes outside a definition's
  handler. Migration is incremental: the ribbon 地图 tab, the layer-tree
  context menu and the palette (Ctrl+Shift+P host) are registry projections
  today; remaining menu rows and ribbon tabs keep direct slot wiring until
  their milestone lands (they call the same handlers, so nothing diverges).

## 11. Command palette (keyboard-first capability surface)

`workbench/command_palette.cpp` — frameless, keyboard-first popup projecting
the whole registry: fuzzy rank (title prefix > contains > keywords > id),
bounded to 60 rows, recent-commands boost (`workbench/palette/recent`),
unavailable entries stay visible with their reason and cannot run. The
palette never implements anything: activation goes through
`CommandRegistry::trigger(id)`. The shell registers it as the
`app.commandPalette` command (Ctrl+Shift+P owned by the hidden action-host
menubar).

## 12. InspectorHost — consolidated inspection

`workbench/inspector_host.h` replaces "one dock per feature" inspection
drift. Sections (`InspectorSection`) declare `supports(snapshot)` and
populate from authoritative sources only; expensive sections are lazy
(populate-on-show) and `cancelPending()` on selection change. The shell
ships 常规 / 元数据 layer sections in the 检查器 dock; identify/spectral
remain dedicated *tools*, not duplicate layer inspectors.

## 13. Layer tree context menu = registry projection

`LayerTreeMenuProvider` now projects registry commands (缩放到图层, 属性表,
属性, 移除) with the same availability + reasons as the ribbon, and keeps
QGIS default actions for tree-structural operations (rename, group,
feature-count, ordering). The menu text is uniformly Chinese.

## 14. Contracts under test

`test_workbench_host`, `test_selection_context`, `test_command_registry`,
`test_command_palette`, `test_inspector_host` pin the semantics above; the
4.0 guardrails (thin-client law, token parity, shortcut conflicts, schema
form) remain in force.

## 12. Workbench 6.0 additions

### 12.1 Layer lifetime & canvas rendering

- `QgsMapCanvas::stopRenderingAndSettle()` (vendored canvas): blocking cancel
  of any in-flight render job (the canvas-destructor idiom). Callers that are
  about to destroy `QgsMapLayer` objects the canvas may be drawing MUST call
  this first — `QgisDisplayManager::removeLayer/relocateLayer/removeView` and
  the legacy `ActiveViewHost::removeSelectedDisplayLayers` path all do.
- `SelectionContext` tracks its sources through `QPointer` guards, purges
  layers on `QgsProject::layerWillBeRemoved` (tombstones keep a doomed layer
  invisible to snapshots even while the canvas still reports it as the
  current layer), and re-validates cached layer pointers on every query.
- `QgisDisplayManager::viewLayerTree(viewId)` exposes each view's own layer
  tree; `ActiveViewHost::refreshCanvasLayers()` reads the ACTIVE view's tree
  (the main view's registered tree IS the project root, so its behavior is
  unchanged). Secondary views never consume the global checked-layer state.

### 12.2 Command & shortcut ownership

- `CommandRegistry` tracks installed canonical shortcuts as
  `QSet<QString> m_shortcutOwners` — exactly ONE projection per COMMAND may
  install its binding; different commands each own theirs. Installation
  never aborts the process (the pre-6.0 single-string assert is gone).
- `test_shortcut_conflicts` scans every command source (menus, command_defs,
  workbench wiring, ribbon, layer-tree menu) for internal duplicates; the
  registry enforces cross-command uniqueness at registration.

### 12.3 Availability & reasons (ContextRules 2.0)

- `ContextRules::prerequisiteFacts(snapshot)` is the structured fact
  projection (selection kind, raster/vector/SAR, editability, governance
  selection) that Help/Hint surfaces consume.
- `ContextRules::unavailabilityReason` covers every prefix family that
  declares an availability predicate (`layer.*`, `layer.edit.*`, `raster.*`,
  `rs.*`, `sar.*`, `result.*`, `asset.*`); an empty return means available.
  The palette renders the reason on disabled rows and refuses to run them.

### 12.4 Workbench lifecycle (#813)

- `IWorkbench` carries the full lifecycle contract: activate/deactivate,
  context augmentation, dirty state, `hasInFlightCompute()`,
  `requestCancel()` (TaskCenter seam) and `requestClose()`.
- External session benches wire `windowGetter` / `setDirtyFn` /
  `setInFlightFn` / `setCancelFn` / `setCloseFn` to the existing windows
  (classification lab, both georeferencer shells, OBIA). Layout remains
  plain lazy-open (per-invocation dialogs, nothing to track).

### 12.5 Inspector 2.0 & SchemaFormBuilder 3.0

- Inspector sections: General, Metadata, plus Milestone-G Vector (fields /
  counts / editability / selection, capped at 32 rows) and SAR (product
  hint + whitelisted provider-metadata facts, bounded page). Tab switches
  populate the newly shown section; sections survive unsupported
  re-selections (reparented, never destroyed by tab rebuilds).
- Schema hints honored by `SchemaFormBuilder`: `x-ui-unit` (label suffix),
  `x-ui-recommended` (tooltip + accessible description),
  `x-ui-visible-when` (conditional fields — hidden fields are excluded from
  `values()` and `validate()`), `x-ui-soft-min`/`x-ui-soft-max`
  (WARNING-level scientific-reasonability diagnostics that never block).

### 12.6 UI-side scans (#797)

- `RsScanPool` (app/widgets/rs_scan_pool.h) is the ONLY sanctioned pool for
  UI-triggered GDAL scans (ROI statistics, histograms): two workers max,
  generation tokens with `nextGeneration()`/`cancel()` cooperative
  cancellation. `QThreadPool::globalInstance()` is never used for scans.

# Part III — Professional Workbench 7.0

## 15. Shell shutdown/switch policy (goal §A)

`workbench/shutdown_policy.h` is the pure decision layer:
`collectWorkbenchShutdownFacts(WorkbenchHost*)` reads every registered bench
through the IWorkbench lifecycle contract; `planWorkbenchShutdown(facts,
runningTaskCount)` projects dirty benches, in-flight benches and TaskCenter
non-terminal tasks into a `ShutdownPlan`. The shell applies it in
`QgisDesktopWindow::confirmWorkbenchShutdown(actionTitle)`:

1. In-flight work first — one dialog; "取消任务并继续" fires
   `cancelInFlightBenches()` (each bench's own `requestCancel()`) plus
   `TaskCenter::cancelTask` for every non-terminal task, and proceeds. Bounded:
   nothing waits for terminal states.
2. Dirty benches — `requestCloseDirtyBenches()` delegates to each bench's own
   `requestClose()` (external session windows run their save/discard
   confirmation); a refusal aborts the operation.

Wired into `closeEvent` (退出), `newProject` and `openProject`. Nothing is
silently dropped; there is exactly one confirmation pass, never a loop.

## 16. Provenance inspector section (goal §B)

`ProvenanceSection` (inspector tab 溯源, order 40) resolves the selection
through authoritative services only — DataManager providers injected by the
shell (never a copied store): Data-Manager asset ids → governance entity ids
(WorkspaceService `assetById`) → layer source paths (`findByPath`). Per target
it renders: identity/state/revision, the `DerivationRecord` (operator, version,
workflow/run/step or task reference, parameter snapshot capped at 1200 chars,
completion time, execution fingerprint, cache truth), source assets with
revisions, a bounded ancestor chain (depth 6 + visited set), derived outputs,
and governance verification (availability, content fingerprint, verification
stamp). Quality warnings (missing/unavailable assets, unresolved input paths,
stale/unverified governance rows) are listed, never suppressed; unknown fields
render as unknown. Population is synchronous (indexed lookups only).

## 17. Unified processing history (goal §C)

`ProcessingHistoryModel` + `ProcessingHistoryPanel` project TaskCenter tasks
(source-tagged gui/agent/mcp/cli/workflow) and WorkflowRunCoordinator runs —
including Interrupted resumables — into ONE queryable view. Bounded by
contract: newest-first, hard cap `kMaxRows` (5000) with a truthful
`droppedCount` surfaced in the UI; filtering and incremental search run over
the capped set; a QTableView renders it (no widget per row). Actions route
through the same seams as the original submissions: cancel →
`TaskCenter::cancelTask`; rerun → `TaskCenter::enqueueTask` with the stored
algorithmId + parameter snapshot; resume → `WorkflowRunCoordinator::resumeRun`;
open/compare/inspect emit signals the shell routes through the Data/Display
and comparison surfaces. The panel keeps no second task store — every refresh
re-queries the services (coalesced at 250 ms).

## 18. Temporal workbench (goal §D)

`TemporalWorkbenchPanel` lists the project's DataManager temporal collections,
parses the stored descriptor through the authoritative `sicnu::temporal` typed
layer, and renders: a clickable/keyboard-navigable timeline strip, a PAGED
scene browser (`TemporalSceneModel`, 200 rows/page over the date-filtered set
— large collections never render unbounded), QA/cloud-cover summary, current
timestep preview and two-date comparison. Preview/compare route through the
shell's existing raster-load and comparison seams; the UI does no raster I/O.

## 19. Dataset/experiment + model benches (goal §E/§F)

`DatasetExperimentPanel` is a thin client over `sicnu::dataset::DatasetStore`
and `sicnu::experiment::ExperimentStore` — the same stores the CLI/agent
write. The user opens the DB files; the panel only projects: dataset list
(store-paged), versions with status/quality/fingerprint, sample counts with a
bounded first-page preview, label-schema version counts, experiment runs
(status/algorithm/dataset pin) and a JSON metric diff over two selected runs.
`ModelWorkbenchPanel` projects the ModelCatalog: task/framework/device/weights
columns, readiness evaluated ONLY by the runtime layer
(`evaluateRuntimeReadiness`, with its reason verbatim), catalog load issues
verbatim, manifest inspection via `inspect()`, and test inference submitted as
a normal `rs:infer` TaskCenter task (failures surface in the processing
history, not in a bespoke dialog). Neither panel persists anything.

## 20. Command surface (goal §I)

The four benches are CommandRegistry commands (`workbench.processingHistory`,
`workbench.temporal`, `workbench.datasetExperiment`, `workbench.model` —
Ctrl+Shift+H/T/E/M; D is taken by map.measureDistance). The registry owns the shortcuts; the 窗口 menu projects
`registry->action(id, true)` instead of defining competing sequences.

## Contracts under test (7.0)

`test_workbench_shutdown_policy`, `test_provenance_section`,
`test_processing_history_model`, `test_temporal_scene_model` — see
`.planning/professional-workbench-7/TEST_MATRIX.md`.

# Part IV — Professional Workbench 8.0

## 21. SchemaForm 4.0 (goal §B — schema-driven form contract)

`SchemaFormBuilder` (`src/app/shell/schema_form_builder.cpp`) stays the single
schema→form seam. 4.0 additions are schema-driven only — no per-operator
logic anywhere:

- **Nested objects**: a property with `type:"object"` AND `properties` (and
  nesting depth < `kMaxObjectDepth` = 4) renders as a recursive sub-group;
  `values()` nests under the key; `validate()` honors the nested `required`
  list. A non-required group whose fields are all empty counts as ABSENT:
  `values()` omits the key entirely and `validate()` skips the group — the
  two agree by construction. Deeper objects degrade to the 3.0 JSON text
  editor. Nested layer/asset/model combos receive the same pushed choice
  sets as top-level fields.
- **Object arrays**: `type:"array"` whose `items` declare an object schema
  render as repeatable item editors (添加/移除; `minItems` seeds rows and
  gates removal, `maxItems` gates the add button). Import via `setValues` is
  bounded by `kMaxObjectArrayItems` = 256 with a visible honest-truncation
  hint naming both totals. Validation issues carry positional dotted paths
  (`points.0.lat`).
- **Dynamic enum sources**: `x-ui-enum-source` resolves through an injected
  `SchemaEnumProvider` (the shell binds authoritative services; the form
  never invents choices). `refreshChoices()` re-queries the provider with the
  current `values()` snapshot, so long-lived forms go stale never. A missing
  provider / unknown / empty source degrades to editable free text with a
  visible tooltip hint — never a silent dead list.
- **Async value checks**: `x-ui-check: "path_exists"` (string or array) runs
  on the bounded `RsScanPool`, debounced 350 ms after edits and forced by
  `runAsyncChecksNow()` after programmatic `setValues` (TaskPanelHost calls
  this on restore). Results are generation-gated widget marks delivered
  through QPointer-guarded queued calls — a newer rebuild/edit supersedes
  older results, a destroyed form or editor drops them, and a cleared value
  resets the mark. Failures append a 路径不存在 tooltip hint that survives
  validation-mark refreshes; checks never block Run — they inform.
  Any host MAY install a `SchemaEnumProvider` / check pool; without one,
  enum sources degrade to free text and checks default to the shared pool.
- **Accessibility**: every editor at every nesting level carries
  `accessibleName` (schema label) and `accessibleDescription` (canonical
  tooltip), labels keep `setBuddy`, and conditional visibility toggles the
  row label together with the editor.

Scalar arrays (comma/semicolon/newline line edits) behave exactly as 3.0 —
backward compatibility is pinned by `test_schema_form_builder_v2`.

## 22. AssetPreviewService (goal §E — lazy previews)

`src/app/preview/asset_preview_service.{h,cpp}` is the single owner of
catalog-scale previews:

- Jobs run on the bounded `RsScanPool` (the #797 two-worker UI-scan pool).
  Never `QThreadPool::globalInstance()`, never the GUI thread.
- Raster pixels flow through `sicnu::geo::RasterReader::readWindowResampled`
  with `OverviewPolicy::Nearest` — the seam the geospatial contract documents
  for preview surfaces. RGB from bands 1-3 (grayscale otherwise), per-band
  min/max stretch excluding NoData/NaN, NoData → black, flat data → gray.
- Vector previews render through `QgsMapRendererCustomPainterJob` (QGIS stays
  the only render engine), on the worker thread with a thread-local layer.
  Layers above `kMaxVectorFeatures` (200k) refuse with `Unsupported` naming
  the count — an honest refusal instead of an unbounded render.
- `requestPreview(request, receiver, callback)` delivers at most once on the
  service's thread. A newer request for the same receiver supersedes older
  ones; canceled tokens and dead receivers drop silently. No UAF after
  teardown (QPointer + hadReceiver guard + queued invoke).
- Cache is LRU bounded by entries (64) and bytes (32 MiB), keyed by
  kind|path|size|mtime|target size — replaced files invalidate naturally.
- The Data Manager detail pane consumes the service lazily on selection;
  remote maps / virtual rasters / missing sources honestly show no preview.

## 23. Data Manager catalog scaling (goal §D — large metadata UI)

`src/app/panels/asset_catalog_index.{h,cpp}` + the reworked
`DataManagerPanel::refresh`:

- `AssetCatalogIndex` is a light, incrementally-maintained projection of the
  catalog (one entry per asset: id/name/source/kind/state/persistence).
  DataManager stays the only catalog authority; the index is a disposable
  projection healed by `rebuild()` and maintained from the per-asset
  signals. Refresh no longer re-fetches every full snapshot. Collection
  MEMBERSHIP is deliberately not mirrored (addChildToCollection emits no
  per-asset signal): children always render from the authoritative
  collection snapshot's `childAssetIds`, so membership can never drift out
  of view.
- Incremental **filter box** (`dataManagerFilter`): case-insensitive
  substring over display name / source / id, coalesced 250 ms, single
  filter+group pass per refresh (O(assets) light comparisons).
- **Lazy collection children**: collections above `kLazyChildThreshold` (50)
  children defer population to first expand (`kLazyPopulateRole`); smaller
  collections render eagerly so the default-expanded layout is unchanged.
- **Bounded truthful rendering**: standalone assets (and per-collection
  children) render up to `m_standaloneRowCap` (default 20 000, host-lowerable
  via `setStandaloneRowCap`); past the cap a non-selectable sentinel row
  names the exact totals — truncation is never silent.
- Selection preservation and the panel's test API (`rowText`,
  `selectedAssetId`, …) are unchanged; `test_data_manager_panel` stays the
  parity suite. Note `rowCount()` counts RENDERED top-level rows — it
  includes the truncation sentinel and is capped by the standalone row cap;
  it is never a catalog total.

## 24. Context facts + suggested next action (goal §C)

`SelectionContextSnapshot` gains `hasInFlightTask` (injected predicate — the
shell binds TaskCenter; the pure layer never touches processing types) and
`ContextFacts` exposes `hasBrokenLayer` + `hasInFlightTask`.
`ContextRules::suggestedNextAction` is a deterministic projection
(editing → raster → vector-editable → temporal → in-flight → empty
workspace) returning a **registered** command id plus human text; the
palette/tests pin that a suggestion can always execute. Broken-layer and
governance-selection facts stay in `ContextFacts` — the registry has no
relocate/open-result commands yet, and a suggestion that cannot execute
would be noise.

## Contracts under test (8.0)

`test_schema_form_4`, `test_asset_preview_service`,
`test_asset_catalog_index` (index + panel + 200k-record scale evidence),
`test_context_facts_8` — see
`.planning/professional-workbench-8/TEST_MATRIX.md`.
