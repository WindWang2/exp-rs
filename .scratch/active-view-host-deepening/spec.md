# Specification: Deepening ActiveViewHost Canvas Sync & Viewport Authority

## Problem Statement

Currently, `ActiveViewHost` delegates active view resolution and canvas pointer queries to `QgisDisplayManager`. GUI widgets and agents must query both `ActiveViewHost` and raw `QgsMapCanvas` pointers to perform viewport synchronization and spatial extent calculations.

## Solution

Deepen `ActiveViewHost` as the sole high-level display authority for active view canvas operations, extent synchronization, and viewport snapshot reporting, encapsulating raw canvas pointer access behind high-level facade methods.

## User Stories

1. As a Developer, I want `ActiveViewHost` to provide a complete viewport snapshot API (`viewportSnapshot()`) so that workspace snapshot capture requires no raw canvas dereferencing.
2. As a GUI Component, I want `ActiveViewHost` to encapsulate canvas refresh and extent zooming without exposing raw `QgsMapCanvas` pointers.
3. As a Headless Test, I want canvas extent and zoom assertions to be testable against `ActiveViewHost`.

## Implementation Decisions

- **Viewport Snapshot API**: Add `ViewportSnapshot viewportSnapshot() const` to `ActiveViewHost` returning extent, scale, CRS, and active layer name.
- **Canvas Extent Encapsulation**: Provide high-level extent setters (`setExtentAndRefresh`) that handle CRS transformation and overview canvas sync automatically.

## Testing Decisions

- **Seam**: Test `ActiveViewHost` using `test_active_view_host_viewport.cpp`.
- **Tests to build/update**:
  - `test_active_view_host_viewport.cpp`: Add unit tests for `viewportSnapshot()` and extent encapsulation.

## Out of Scope

- Refactoring QGIS core `QgsMapCanvas` rendering pipelines.

## Further Notes

Aligned with ADR 0010 (Data/Display Seams), ADR 0018, and ADR 0053.
