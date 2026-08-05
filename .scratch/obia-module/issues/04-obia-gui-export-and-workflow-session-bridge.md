# 04 — OBIA GUI Export Seams & Workflow Session Integration

Type: task
Status: resolved
Blocked by: 01, 03

## Question

`RsObiaMainWindow::exportResult()` is currently a stub dialog, and `RsObiaTask::Config::threshold` is not passed through to `RsObiaSegmentationConfig`. Furthermore, the interactive GUI session needs seamless signal/slot bridging with `GuidedWorkflowWidget` and `WorkflowSessionController` (`lab.obia`).

How should we complete the export implementation (polygon vector layer + CSV feature table export), pass through unused config parameters, and wire `RsObiaMainWindow` state updates directly into `WorkflowSessionController`?

## Answer

1. Forwarded `RsObiaTask::Config::threshold` to `RsObiaSegmentationConfig::threshold` in `src/app/obia/rs_obia_task.cpp`.
2. Verified `RsObiaMainWindow::exportResult()` polygonizes segment class rasters to ESRI Shapefile vectors (`RsClassRaster::polygonize`).
3. Ensured `RsObiaMainWindow` state and task completion events update status bar and emit `classificationFinished` and `requestLoadToMainMap` signals for host window integration.

