# 01 — Real Data Range Statistics & Axis Scaling

**Type:** wayfinder:grilling

## Question

How should real pixel data statistics (Min, Max, NoData) be dynamically extracted via GDAL/QGIS and formatted on `HistogramWidget`'s X-axis and spin boxes for 16-bit and 32-bit Float rasters?

## Blocked by

None — can start immediately.

**Status:** closed

### Resolution
- Extracted real physical pixel statistics ($\text{DataMin}, \text{DataMax}$) via GDAL/QGIS band statistics.
- Formatted X-axis and spin boxes to match exact data bounds (16-bit / 32-bit Float).
- Documented in [0008-real-data-range-and-piecewise-linear-stretch.md](../../docs/adr/0008-real-data-range-and-piecewise-linear-stretch.md).
