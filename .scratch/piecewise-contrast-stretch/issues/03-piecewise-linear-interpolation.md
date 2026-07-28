# 03 — Piecewise Linear Interpolation & Live Map Canvas Mapping

**Type:** wayfinder:grilling

## Question

How should piecewise linear mapping segments be evaluated and applied to `QgsSingleBandGrayRenderer` and `QgsMultiBandColorRenderer` via custom color ramp / contrast enhancement functions?

## Blocked by

02 — Piecewise Control Point Handles on Histogram Canvas

**Status:** closed

### Resolution
- Evaluated piecewise linear interpolation $y(x) = y_i + \frac{x - x_i}{x_{i+1} - x_i} (y_{i+1} - y_i)$ across control segments.
- Applied live mapping to map canvas renderers.
- Documented in [0008-real-data-range-and-piecewise-linear-stretch.md](../../docs/adr/0008-real-data-range-and-piecewise-linear-stretch.md).
