# Specification: Real Data Range & Piecewise Linear Contrast Stretch

## Problem Statement

Remote sensing imagery often uses 16-bit unsigned integers (DN values `0 .. 65535`) or 32-bit floating point reflectance values (`-0.1 .. 1.2`). Standard 8-bit $[0, 255]$ clamping causes clipping and loss of dynamic range. Furthermore, complex terrain or atmospheric conditions require **Piecewise Linear Stretch (分段线性拉伸)** with arbitrary control point handles $(x_i, y_i)$ to enhance specific spectral ranges (e.g. shadow details or bright urban surfaces).

## Solution

Upgrade **`HistogramWidget`**, **`HistogramStretchWidget`**, and **`ImageEnhancement`**:
1. **Real Physical Data Range Display**: Query GDAL/QGIS band statistics ($\text{DataMin}$, $\text{DataMax}$) dynamically. Scale `HistogramWidget`'s X-axis, labels, and SpinBoxes to the raster's physical value bounds.
2. **Piecewise Linear Control Points (分段点 & 折线控制)**:
   - Render interactive circular handles ($\bigcirc$) connected by linear segment lines overlaid across the histogram.
   - Left-click drag to move control points.
   - Double-click to insert new control points.
   - Right-click to remove intermediate control points.
3. **Piecewise Linear Interpolation & Live Rendering**:
   - Compute output pixel value $y(x) = y_i + \frac{x - x_i}{x_{i+1} - x_i} (y_{i+1} - y_i)$ for $x \in [x_i, x_{i+1}]$.
   - Apply live mapping to QGIS map canvas renderers.
4. **GeoTIFF Export Algorithm**:
   - Implement `ImageEnhancement::piecewiseLinearStretch` and dispatch async execution via `TaskCenter`.

## User Stories

1. As a Remote Sensing analyst, I want the histogram to show the true physical pixel value range of my 16-bit / Float GeoTIFF image (e.g. `0 .. 4096`), so that values are not artificially scaled or clamped to 255.
2. As a user, I want to double-click on the histogram chart to add custom piecewise control points $(x_i, y_i)$, so that I can stretch specific spectral ranges independently.
3. As a user, I want to drag control points with the mouse and see the map canvas update in real time.
4. As a user, I want to right-click a control point to delete it, returning that segment to linear interpolation.
5. As a user, I want to export the piecewise stretched raster as a new GeoTIFF file via `TaskCenter`.

## Implementation Decisions

- `HistogramWidget`: Add `QVector<QPointF> m_piecewisePoints`. Implement `mouseDoubleClickEvent` to insert points and right-click context/click to delete.
- `ImageEnhancement`: Implement `piecewiseLinearStretch(const float* input, float* output, size_t count, const std::vector<std::pair<float, float>>& points)`.
- `ContrastStretchDialog`: Integrate piecewise stretch preset and control point list editor.

## Out of Scope

- Non-linear cubic Hermite / B-Spline curve fitting (piecewise linear interpolation is used).

## Further Notes

- Documented in ADR: [`0008-real-data-range-and-piecewise-linear-stretch.md`](../../adr/0008-real-data-range-and-piecewise-linear-stretch.md).
