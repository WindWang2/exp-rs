# ADR 0043: Route SicnuPythonApi Through ActiveViewHost

## Status
Accepted

## Context
`SicnuPythonApi` had inconsistent seam discipline: `addRasterLayer` and
`addVectorLayer` correctly routed through `ActiveViewHost`, but seven other
methods (`removeLayer`, `canvasExtent`, `setCanvasExtent`, `refreshCanvas`,
`canvasScale`, `setCanvasScale`, `setProjectCrs`) reached directly into a
raw `QgsMapCanvas*` member and `QgsProject::instance()`. This broke the
Data/Display seam (ADR 0009/0010/0015) and prevented headless testing of the
Python API.

## Decision
1. **Remove `m_canvas` member and `initialize(QgsMapCanvas*)`**: The singleton
   no longer holds a raw canvas pointer. All canvas access goes through
   `m_activeViewHost`.

2. **Route canvas operations through ActiveViewHost**: `canvasExtent`,
   `setCanvasExtent`, `refreshCanvas`, `canvasScale`, `setCanvasScale` now
   delegate to `ActiveViewHost::mapCanvasExtent()`, `setExtent()`,
   `refreshCanvas()`, `mapCanvasScale()`, and a new `setScale(double)`.

3. **Route refresh-after-mutation through ActiveViewHost**: `removeLayer` and
   `setProjectCrs` now call `m_activeViewHost->refreshCanvas()` instead of
   `m_canvas->refresh()`.

4. **New `ActiveViewHost::setScale(double)`**: Added to complement the existing
   viewport methods (ADR 0036). Delegates to `QgsMapCanvas::zoomScale()` with
   null guard.

## Consequences
- **Data/Display seam fully sealed in Python API**: no `QgsMapCanvas*` leaks.
- **Headless testable**: stub `ActiveViewHost` covers all Python API paths.
- **Breaking change**: `SicnuPythonApi::initialize(QgsMapCanvas*)` removed.
  The single caller in `main_window.cpp` has been updated.
