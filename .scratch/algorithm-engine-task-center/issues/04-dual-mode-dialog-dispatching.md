# 04 — Dual-Mode Dialog Dispatching & Algorithm Migration

**What to build:** Upgrading algorithm dialogs (Classification, OBIA, Georeferencing SIFT, Spectral processing, GDAL tools) to dispatch via `TaskCenter`, adding the "Run in Background" dual-mode feature, and auto-loading result layers onto the map canvas.

**Blocked by:** 03 — TaskCenterDock UI Workspace & Main Window Integration

**Status:** completed

- [x] Update `AsyncAlgorithmRunner` and processing dialog bases to dispatch through `TaskCenter`
- [x] Add "Run in Background" button behavior to algorithm dialogs
- [x] Integrate OBIA, Classification, and SIFT tasks into `TaskCenter` dispatching
- [x] Connect auto-load signal to main map canvas layer tree upon task completion
