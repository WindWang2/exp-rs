# 04 — GeoTIFF Piecewise Linear Backend Algorithm

**Type:** wayfinder:task

## Question

How should the backend algorithm `ImageEnhancement::piecewiseLinearStretch` apply the piecewise transformation to multi-band GeoTIFF rasters via `TaskCenter`?

## Blocked by

03 — Piecewise Linear Interpolation & Live Map Canvas Mapping

**Status:** closed

### Resolution
- Implemented `ImageEnhancement::piecewiseLinearStretch` backend logic.
- Dispatched async execution via `TaskCenter`.
- Documented in [0008-real-data-range-and-piecewise-linear-stretch.md](../../docs/adr/0008-real-data-range-and-piecewise-linear-stretch.md).
