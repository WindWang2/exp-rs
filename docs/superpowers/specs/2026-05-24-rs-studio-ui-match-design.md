# RS Studio — Match App UI to Prototype

**Date:** 2026-05-24
**Status:** Approved (design), pending implementation plan
**Author:** brainstorming session

## Goal

Reskin the PyQt/PySide6 application so its UI matches the `RS Studio (standalone).html`
design prototype as closely as possible ("完全一致"). The prototype is a React design
canvas with 10 artboards; this work covers:

1. **Artboard 01 — Main Workspace** (the primary target), rebuilt as the real app's
   main window.
2. The **existing dialogs** (`gui/prototype_views.py` + `ToolParameterDialog` +
   `LayerPropertiesDialog`), restyled to match their corresponding artboards.

The other artboards (AI Agent full workspace, design tokens reference) are out of scope
for this pass.

## Decisions (locked during brainstorming)

- **Scope:** Main workspace + existing dialogs.
- **Content strategy:** Layout and styling are reproduced *exactly*; panel **contents are
  wired to real data** (real layers, real toolbox tools, real logs, real layer metadata,
  real canvas state). Static mock content is used only as placeholders where no real
  backing exists (e.g. cache size, render-time readouts in the status bar).
- **Panel architecture:** Fixed 3-column layout built with `QSplitter` + **custom panel
  headers**, replacing the current dockable `QDockWidget` chrome.
- **Fonts:** Bundle IBM Plex Sans / IBM Plex Mono and load at startup via `QFontDatabase`.
- **Fake satellite scene is dropped** — the center map renders real imagery; only the
  prototype's overlay *chrome* (band-combo toggles, north arrow, scale bar, coordinate
  readout, AOI box, center crosshair) is reproduced on top.
- **AI 助手** is a toggle (toolbar button + 视图 menu), hidden by default — matching the
  prototype's main workspace, where AI appears only as a button (the full AI workspace is
  a separate artboard, out of scope).

## Design system reference

Source of truth: `scratch_template.html` (CSS tokens) and `scratch_extracted/*.js`
(component markup). Key tokens to port into `resources/styles.qss`:

- **Neutrals:** bg-0 `#ffffff`, bg-1 `#f6f7f9`, bg-2 `#ffffff`, bg-3 `#eef1f5`,
  bg-4 `#e2e6ec`, bg-5 `#cfd5dd`, bg-6 `#b3bac4`, map-bg `#e9ecf0`.
- **Lines:** line-1 `#e4e7eb`, line-2 `#d4d8de`, line-3 `#b8bec7`.
- **Text:** t-0 `#14171c`, t-1 `#2f3640`, t-2 `#5b6473`, t-3 `#8a92a0`, t-4 `#b8bec7`.
- **Accent:** ac `#1f6feb`, ac-2 `#0d5fcc`, ac-soft `rgba(31,111,235,0.10)`,
  ac-line `rgba(31,111,235,0.42)`, ac-on `#ffffff`.
- **Semantic:** ok `#1a7f37`, warn `#bf8700`, err `#cf222e`.
- **AI:** ai `#8250df`, ai-soft `rgba(130,80,223,0.09)`, ai-line `rgba(130,80,223,0.42)`.
- **Type:** sans = IBM Plex Sans, mono = IBM Plex Mono; sizes xxs 10 / xs 11 / sm 12 /
  md 13 / lg 15 / xl 18 px.
- **Geometry:** radii 2/3/5/8 px; row-h 22, tool-h 28, header-h 26.
- **Component heights:** menubar 28, toolbar 36, statusbar 22, tab strip 26.

## Architecture

### New files

| File | Responsibility |
|------|----------------|
| `resources/fonts/` | Bundled IBM Plex Sans + Mono (ttf/otf). |
| `gui/rs_icons.py` | `ICON_PATHS` dict (≈60 named SVG paths ported verbatim from the prototype's `icons.jsx`) + `rs_icon(name, size=14, color="#2f3640")` rendering an SVG string through `QSvgRenderer` to a recolorable `QIcon`/`QPixmap`. Stroke = color, stroke-width 1.5, fill none, 16×16 viewBox. |
| `gui/rs_widgets.py` | Custom chrome toolkit (see below). |
| `gui/workspace.py` | `RsWorkspace` — assembles the 3-column layout and exposes the embedded canvas / models / panels to `MainWindow`. |

### `gui/rs_widgets.py` toolkit

- **`RsPanel(title, icon=None, actions=None)`** — `QFrame` with a 26px gradient header
  (`.ph`: uppercase, 0.08em letter-spacing, t-0 bold title, leading icon, right-aligned
  18px action buttons) and a body container with `addBodyWidget()`. Replaces dock chrome.
- **`RsTabBar(tabs, active)`** — 26px horizontal tab strip; active tab has bg-0 fill and a
  2px accent underline; optional trailing count badge. Emits `tab_changed(id)`.
- **`RsToolBar`** — custom `QWidget`, 36px, holding groups separated by 1px dividers. Each
  button is icon-only or icon+label, 26px, hover = bg-3, `on` (checkable active) =
  ac-soft fill + ac-line border + ac text. `add_group([...])`; buttons emit `triggered(id)`
  and support checkable/exclusive groups (nav tools).
- **`RsStatusBar`** — custom `QWidget`, 22px, mono font, segments divided by 1px lines:
  ready dot + 就绪, coordinate, scale, CRS, spare; right-aligned: thread/task count,
  render ms, cache size. Public setters: `set_coord`, `set_scale`, `set_crs`,
  `set_message`. Cache/render values are placeholders.
- **`RsConsole`** — bottom console: `RsTabBar` (日志/任务/Python 控制台/历史) + a mono
  read-only log view fed by `core.logger`. Log lines render as `time | LEVEL | module |
  message` with level colors (INFO=ok, WARN=warn, ERROR=err). Tabs beyond 日志 may be
  placeholder panes.
- **`RsRowDelegate(QStyledItemDelegate)`** — paints `.rs-row`: indent by depth, twig
  chevron (down/right/leaf), optional checkbox (12px, accent when checked), optional color
  swatch (12px), optional leading icon, name (elided), mono meta on the right; hover = bg-3;
  selected = ac-soft fill + 2px left accent bar. Driven by item data roles. Reused by the
  layer tree, toolbox tree, and data-browser tree. Row height 22px.
- **`RsPropertyPanel(QWidget)`** — `RsTabBar` (信息/符号化/直方图/元数据) over a scroll
  area of `Section` blocks (数据源 / 坐标系 / 拉伸) built from `Prop` rows (label t-2 left,
  mono value t-0 right) plus a stretch histogram widget. `set_layer(layer)` populates from
  real metadata; empty state when no layer selected.

Shared helpers: `Section(label)` block header (uppercase 10px t-3), `prop_row(k, v, mono)`,
`RsSearchInput` (24px bg-1 input with leading search icon).

### `gui/workspace.py` — `RsWorkspace(QWidget)`

Vertical layout:
1. `RsToolBar` (groups, in prototype order).
2. Horizontal `QSplitter`:
   - **Left** (default 280px): vertical `QSplitter` → `RsPanel("数据浏览器")` containing a
     data-browser `QTreeView` (≈260px) over `RsPanel("图层")` containing the layer
     `LayerTreeView` (stretch).
   - **Center**: `QVBoxLayout` → `RsTabBar(地图视图 1 / 布局视图 / 3D 视图)` →
     map-overlay container (the existing `MapCanvas` + overlay children) → a vertical
     `QSplitter` boundary with `RsConsole` (default 160px).
   - **Right** (default 320px): vertical `QSplitter` → `RsPanel("处理工具箱")` (search +
     toolbox tree, 50%) over `RsPanel("图层属性")` (`RsPropertyPanel`, 50%).
3. `RsStatusBar`.

`RsWorkspace` exposes `.canvas`, `.layer_view`, `.layer_model`, `.toolbox`,
`.property_panel`, `.console`, `.status` so `MainWindow` wires signals as it does today.

### Map overlay container

The center map is the existing `gui/canvas.py:MapCanvas` (real `QGraphicsView` renderer).
A container `QWidget` holds the canvas plus overlay child widgets positioned in
`resizeEvent`:

- top-left image-info pill (scrim bg, mono) — shows active raster name / date / band combo
  when available, hidden otherwise;
- top-right band-combo toggle buttons (真彩色 / 假彩色 / NDVI / 分类) — switch the active
  raster's render style where supported, else cosmetic-only;
- circular north arrow (top-right, below combo);
- scale bar (bottom-left) — derived from real canvas scale;
- faint center crosshair.

Coordinate / scale / CRS values come from real `MapCanvas` state via existing signals.

### Menu bar

`QMainWindow.menuBar()` styled as `.rs-menubar`. Items, in order:
文件 · 编辑 · 视图 · 图层 · 处理 · 栅格 · 矢量 · 数据库 · AI 助手 · 插件 · 窗口 · 帮助.
Brand ("RS" logo chip + "RS Studio") as a left corner widget; right corner widget = version
label + bell + user icons. Real actions populate 文件 (add raster/vector, save), 视图 (panel
toggles, AI toggle), 遥感/处理 (the prototype dialogs). Menus without real backing render as
empty/placeholder menus.

### Wiring real data

- **Layer tree:** extend `LayerTreeModel` items with swatch-color and meta data roles so
  `RsRowDelegate` can paint them; keep existing visibility/reorder/context-menu signals.
- **Toolbox:** keep `engine.registry.ToolRegistry`; render its categories/tools through a
  tree using `RsRowDelegate` (folder/cog/wand/brain icons, per-category counts). Keep
  double-click → `ToolParameterDialog` → `tool_triggered`.
- **Console:** subscribe to `core.logger` output.
- **Properties:** `RsPropertyPanel.set_layer()` from the selected layer; status-bar
  coord/CRS/scale from `MapCanvas`.
- **AI 助手:** keep `AgentDockWidget` as a right `QDockWidget`, hidden by default, toggled
  by the toolbar AI button and the 视图 menu.

### Dialogs

Restyle to match their artboards using the toolkit (RsPanel headers, RsTabBar, Section/Prop
rows, Btn variants, rs_icons):
`SpectralProfileDialog`, `ModelBuilderDialog`, `GeorefDialog`, `ROIEditorDialog`,
`AttributeTableDialog`, `ToolParameterDialog`, `LayerPropertiesDialog`.

### QSS

Extend `resources/styles.qss`: full token palette, IBM Plex font-family on `QWidget`, and
objectName-scoped rules for the new custom widgets (`RsPanel`, `RsTabBar`, `RsToolBar`,
`RsStatusBar`, `RsConsole`, property rows, search input, scrollbars already present).

## Open implementation risks

- **woff2 loading:** `QFontDatabase.addApplicationFont` may not accept `.woff2`. Mitigation:
  convert the bundled fonts to `.ttf`/`.otf` (fonttools) or fetch upstream IBM Plex TTFs.
  Verify early in implementation.
- **Overlay repositioning:** overlay widgets must track the canvas viewport on resize and
  not intercept map mouse events (set `WA_TransparentForMouseEvents` except on interactive
  toggles).
- **Delegate reuse:** the three trees use different models (QStandardItemModel for layers,
  QTreeWidget for toolbox); `RsRowDelegate` must read role data defensively.

## Out of scope

- AI Agent full workspace artboard, Model/Spectral/etc. *new* behavior — only visual
  restyle of existing dialogs.
- The design-tokens reference artboard.
- Splash screen redesign (kept as-is).

## Build sequence (high level — detailed in the plan)

1. Fonts + `rs_icons.py` + QSS tokens (foundation).
2. `rs_widgets.py` toolkit with isolated visual verification.
3. `workspace.py` assembly with placeholder data.
4. Wire real backends (layers, toolbox, console, properties, status, overlays).
5. Menu bar + toolbar actions + AI toggle.
6. Restyle dialogs.
7. Full app run-through and polish pass against the prototype.
