# Antigravity RS: Developer Guidelines

Welcome to the standalone Remote Sensing analysis platform codebase.

## Quick Commands

*   **Launch GUI App:** `python main.py`
*   **Run Automated Tests:** `PYTHONPATH=. pytest`
*   **Run Single Test File:** `PYTHONPATH=. pytest tests/test_reader.py`
*   **Compile C++ Extension (Pybind11):**
    ```bash
    mkdir build && cd build
    cmake ..
    make
    ```

## Codebase Architecture

*   `core/`: Pure modular QGIS-emulated GIS core (GeospatialReader, QgsMapLayer, QgsLayerTreeNode, coordinate transform caching).
*   `analysis/`: Geospatial analytical tools (spectral indexes, pansharpening, unsupervised classification).
*   `gui/`: Premium PyQt/PySide6 widgets (MapCanvas, LayerTreeView/Model, ProcessingToolbox, splash screens).
*   `agent/`: Sandboxed NLP-to-JSON tool dispatchers and offline executor.
*   `src/`: Pybind11 C++ shared library source code (`raster_ops.cpp`).
*   `tests/`: Automated PyTest unit and integration test suites.

## Coding Style & Standards

1.  **Strict Thread Isolation:** Never share a single `rasterio` or `fiona` file handle across background threads. Every background thread worker must open its own separate `GeospatialReader` or `rasterio.open(...)` handle to prevent concurrent C++ GDAL segfaults.
2.  **No process-level eval() calls:** Keep the AI Agent execution 100% sandboxed using our declarative non-evaluating `ToolRegistry` and JSON tool dispatches.
3.  **Cosmetic Vector Painting:** All paths painted on `MapCanvas` must set `QPen.setCosmetic(True)` to remain sharp regardless of viewport zoom scales.
4.  **MEM-Safety in PyQt:** Always construct detached copies of numpy image arrays (`q_img.copy()`) when mapping memory matrices to `QPixmap` buffers in background loops.

---

## Skill routing

When a developer's request matches an available skill, invoke it via the Skill tool.

Key routing rules:
- Product ideas/brainstorming → invoke /office-hours
- Strategy/scope → invoke /plan-ceo-review
- Architecture → invoke /plan-eng-review
- Design system/plan review → invoke /design-consultation or /plan-design-review
- Full review pipeline → invoke /autoplan
- Bugs/errors → invoke /investigate
- QA/testing site behavior → invoke /qa or /qa-only
- Code review/diff check → invoke /review
- Visual polish → invoke /design-review
- Ship/deploy/PR → invoke /ship or /land-and-deploy
