# Wayfinder Map: Real Data Range Display & Piecewise Linear Contrast Stretch

## Destination

Implement Real Data Range Display (16-bit/Float raw min-max) and Piecewise Linear Contrast Stretch (分段线性拉伸) with interactive control point handles on the histogram canvas, real-time map canvas rendering, and backend GeoTIFF algorithm task dispatching.

## Notes

- Domain Glossary: `CONTEXT.md` (Real Data Range, Piecewise Linear Stretch, Algorithm Engine, Task Center)
- ADRs: `docs/adr/0001-algorithm-engine-and-task-center.md`
- Primary C++ Modules: `src/app/widgets/histogram_widget.{h,cpp}`, `src/app/widgets/histogram_stretch_widget.{h,cpp}`, `src/processing/algorithms/image_enhancement.{h,cpp}`

## Decisions so far

- [Destination & Scope](docs/adr/0001-algorithm-engine-and-task-center.md) — 16-bit/Float Real Data Range + Interactive Piecewise Control Point Handles on Histogram Canvas.
- [01 — Real Data Range Statistics & Axis Scaling](issues/01-real-data-range-statistics.md) — Dynamic GDAL/QGIS band min/max extraction for 16-bit / 32-bit Float rasters.
- [02 — Piecewise Control Point Handles on Histogram Canvas](issues/02-piecewise-control-point-handles.md) — Interactive node handles ($\bigcirc$) with left-click drag, double-click add, right-click delete.
- [03 — Piecewise Linear Interpolation & Live Map Canvas Mapping](issues/03-piecewise-linear-interpolation.md) — Multi-segment linear mapping $y(x)$ applied live to QGIS renderers.
- [04 — GeoTIFF Piecewise Linear Backend Algorithm](issues/04-geotiff-piecewise-linear-algorithm.md) — `ImageEnhancement::piecewiseLinearStretch` backend dispatched via `TaskCenter`.

## Not yet specified

- Spline Curve (B-Spline / Cubic Hermite) non-linear interpolation (out of scope for linear piecewise).

## Out of scope

- 3D Color Cube LUT transformations.
