# SICNU GEO RS: Developer Guidelines

Pure C++ Remote Sensing analysis platform built on the QGIS engine.

## Quick Commands

*   **Build:** `cd build && cmake .. && make -j$(nproc)`
*   **Launch:** `./build/sicnu_geo_rs`
*   **Clean build:** `rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)`

## Codebase Architecture

See [docs/repo-layout.md](docs/repo-layout.md) for the full directory map.

*   `src/app/main.cpp`: Pure C++ Qt6 desktop application (QgisDesktopWindow).
*   `src/core/`: QGIS core library — layers, rendering, CRS, geometry, providers, expressions.
*   `src/gui/`: QGIS GUI library — map canvas, map tools, layer tree, dialogs.
*   `src/analysis/`: Analysis libraries — classification, georeferencing, segmentation.
*   `src/agent/`: AI Agent infrastructure — MCP server, LLM copilot, unified Agent Tool Catalog (`tool_catalog/`), spatial tools (`spatial_tools/`, ADR 0122: `spatial:raster_inspect`, `spatial:vector_inspect`, `spatial:list_models`). STAC client lives in `src/app/` (ADR 0050).
*   `src/processing/`: Processing framework — algorithms, GDAL wrappers, providers, Tool Call Dispatcher, Task Center, algorithm capability sidecar store (`framework/algorithm_meta_store.*`). Toolbox coverage manifest: `data/processing/toolbox_manifest.json`; algorithm capability sidecars: `data/processing/algorithm_meta/*.json`; Generic CLI tools: `data/tools/custom/*.json`.
*   `src/operators/`: RSOperator framework (`framework/`, incl. the model runtime catalog `model_catalog.*`) + `rs:` / `gdal:` / `otb:` / `opencv:` operator families.
*   `pi/`: Pi agent-runtime adapter (ADR 0122) — `exp-rs-spatial.ts` spawns the binary with `--mcp` and bridges MCP tools as Pi tools; `pi/knowledge/` holds the agent algorithm-selection guide.
*   `models/`: Model runtime catalog manifests (`models/*/model.json`); `rs:infer` resolves catalog names to weight paths.
*   `src/native/`: Platform-native integration (Linux/macOS/Windows).
*   `src/ui/`: Qt Designer .ui form files.
*   `external/`: Vendored C++ dependencies (nlohmann_json, spatialindex, poly2tri, lazperf).
*   `data/samples/`: Lab / tutorial sample datasets.
*   `docs/`: Design, architecture, ADR ledger (`docs/adr/0001`–`0122`, lazily created by domain-modeling skills), labs, agent notes, specs/plans (`docs/superpowers/` holds historical design docs).
*   `refs/qgis/`, `refs/boost/`: Optional local reference trees (gitignored).
*   `itk_ref/`, `otb_ref/`: ITK / OTB source at repo root (CMake-coupled).
*   `src/app/`: Application shell — see [P0–P5 refactor spec](docs/superpowers/specs/2026-07-03-refactor-sprint-design.md) for module map:
    *   `main_window.cpp` — constructor, `setupUi`, `setupMapCanvas`
    *   `main_window_menus.cpp` — menu / toolbar / status bar
    *   `main_window_docks.cpp` — dock panels
    *   `main_window_connections.cpp` — signals, canvas state, layer tree events
    *   `main_window_view.cpp` — zoom, pan, measure, Georeferencer
    *   `main_window_vector.cpp` — vector editing
    *   `main_window_project.cpp` — project I/O, STAC browser
    *   `main_window_layers.cpp` — layers, identify results
    *   `main_window_misc.cpp` — preferences, help, panel layout
    *   `main_window_processing.cpp` — RS processing dialog slots
    *   `dialogs/raster_processing_dialog_base.{h,cpp}` — shared async dialog lifecycle

## Language

100% C++ (C++20) in `src/`. No Python at runtime (Python only drives build-time code generation in `scripts/`). The single exception outside the app: `pi/exp-rs-spatial.ts` is a TypeScript Pi-extension adapter (ADR 0122) — it is external tooling for the Pi agent runtime, never compiled into or loaded by the C++ application.

## Coding Style & Standards

1.  All code must be C++. No Python runtime code in the project.
2.  Follow QGIS coding conventions for consistency with vendored QGIS source.
3.  Use Qt6 APIs (QMainWindow, QGraphicsView, QWidget, etc.).
4.  Thread safety: QGIS rendering uses background threads via QgsMapRendererJob.

## Skills

Project skills live under `.agents/skills/` (mirrored to `.claude/skills/` for Claude Code). Prefer those for engineering workflows (`tdd`, `implement`, `to-spec`, `code-review`, …).

Additional vendor skills (see `.agents/vendor/` for provenance):

* **Qt AI skills** ([TheQtCompanyRnD/agent-skills](https://github.com/TheQtCompanyRnD/agent-skills)): `qt-cpp-review`, `qt-qml-review`, `qt-qml`, `qt-ui-design`, `qt-cmake-project`, `qt-cpp-docs`, `qt-qml-docs`, `qt-qml-test`, `qt-qml-test-run`, `qt-qml-profiler`, `qt-figma-token-extraction`, `qt-figma-component-generation`.
* **frontend-design** ([anthropics/claude-code](https://github.com/anthropics/claude-code) plugin): distinctive UI/visual design guidance.

## UI theme (Canopy Lab)

* Light: `resources/styles.qss` (default, Fusion + QSS).
* Dark: `resources/styles-dark.qss` (Preferences → Theme, or `preferences/theme=dark`).
* Signature chrome: `BandCompositionRail` under the ribbon (band chips + Real Data Range).

## Agent skills

### Issue tracker

GitHub Issues on `WindWang2/exp-rs` via `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout: root `CONTEXT.md` + `docs/adr/` (created lazily by domain-modeling skills). See `docs/agents/domain.md`.
