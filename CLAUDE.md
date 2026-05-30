# SICNU GEO RS: Developer Guidelines

Pure C++ Remote Sensing analysis platform built on the QGIS engine.

## Quick Commands

*   **Build:** `cd build && cmake .. && make -j$(nproc)`
*   **Launch:** `./build/sicnu_geo_rs`
*   **Clean build:** `rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)`

## Codebase Architecture

*   `main.cpp`: Pure C++ Qt6 desktop application (QgisDesktopWindow).
*   `src/core/`: QGIS core library — layers, rendering, CRS, geometry, providers, expressions.
*   `src/gui/`: QGIS GUI library — map canvas, map tools, layer tree, dialogs.
*   `src/native/`: Platform-native integration (Linux/macOS/Windows).
*   `src/ui/`: Qt Designer .ui form files.
*   `external/`: Vendored C++ dependencies (nlohmann_json, spatialindex, poly2tri, lazperf).

## Language

100% C++ (C++17). No Python at runtime. Python is only used at build time for code generation scripts (`scripts/`).

## Coding Style & Standards

1.  All code must be C++. No Python runtime code in the project.
2.  Follow QGIS coding conventions for consistency with vendored QGIS source.
3.  Use Qt6 APIs (QMainWindow, QGraphicsView, QWidget, etc.).
4.  Thread safety: QGIS rendering uses background threads via QgsMapRendererJob.
