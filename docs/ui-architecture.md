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
entry points (menu, ribbon tab, toolbar) share **one** QAction — a capability
must never have two divergent code paths.

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
  Inline raster kernels (`runGdalTask`/`callable:` lambdas) in dialogs are
  banned — enforced by `tests/test_ui_task_center_contract.cpp` source scan.
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
  `SicnuUi::Tokens::status*` colors (see §6), in RsJobPanel and TaskPanelHost.
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
  地图 tab absorbed band composition). CLAUDE.md's "signature chrome" note
  refers to history; the live band UI is the ribbon tab.
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
