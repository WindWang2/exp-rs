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
*   `src/agent/`: AI Agent infrastructure — MCP server, STAC client.
*   `src/processing/`: Processing framework — algorithms, GDAL wrappers, providers. Toolbox coverage manifest: `data/processing/toolbox_manifest.json`; Generic CLI tools: `data/tools/custom/*.json`.
*   `src/native/`: Platform-native integration (Linux/macOS/Windows).
*   `src/ui/`: Qt Designer .ui form files.
*   `external/`: Vendored C++ dependencies (nlohmann_json, spatialindex, poly2tri, lazperf).
*   `data/samples/`: Lab / tutorial sample datasets.
*   `docs/`: Design, architecture, labs, agent notes, specs/plans (`docs/superpowers/` holds historical design docs).
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

100% C++ (C++20). No Python at runtime. Python is only used at build time for code generation scripts (`scripts/`).

## Coding Style & Standards

1.  All code must be C++. No Python runtime code in the project.
2.  Follow QGIS coding conventions for consistency with vendored QGIS source.
3.  Use Qt6 APIs (QMainWindow, QGraphicsView, QWidget, etc.).
4.  Thread safety: QGIS rendering uses background threads via QgsMapRendererJob.

## Skills

Project skills live under `.agents/skills/` (mattpocock/skills and others). Prefer those for engineering workflows (`tdd`, `implement`, `to-spec`, `code-review`, …).

## Agent skills

### Issue tracker

GitHub Issues on `WindWang2/exp-rs` via `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout: root `CONTEXT.md` + `docs/adr/` (created lazily by domain-modeling skills). See `docs/agents/domain.md`.
