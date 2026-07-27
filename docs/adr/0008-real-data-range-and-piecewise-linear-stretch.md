# 0008 Real Data Range and Piecewise Linear Contrast Stretch Architecture

We decided to support Real Physical Pixel Data Ranges (16-bit / 32-bit Float) and Piecewise Linear Stretch with interactive control point handles on `HistogramWidget`.

### Context & Decision

1. **Real Data Range Display**:
   - `HistogramWidget` queries exact GDAL/QGIS band statistics ($\text{DataMin}$, $\text{DataMax}$) dynamically.
   - X-axis bounds and input spin boxes adjust dynamically to the layer's physical value range (e.g. 16-bit `0..65535` or Float `-0.2..1.5`) without clamping to 8-bit `0..255`.

2. **Piecewise Control Points on Histogram Canvas**:
   - Control points $(x_i, y_i)$ are stored in a sorted vector, anchored at $(\text{DataMin}, 0)$ and $(\text{DataMax}, 255)$.
   - Displayed as interactive circular node handles ($\bigcirc$) connected by piecewise linear line segments.
   - Mouse interactions: Left-click drag to move, Double-click to add control points, Right-click to remove points.

3. **Piecewise Linear Interpolation**:
   - For any input pixel $x \in [x_i, x_{i+1}]$, output display intensity $y(x)$ is calculated as:
     $$y(x) = y_i + \frac{x - x_i}{x_{i+1} - x_i} (y_{i+1} - y_i)$$
   - Applied live to QGIS map canvas renderers and exported asynchronously via `ImageEnhancement::piecewiseLinearStretch` in `TaskCenter`.
