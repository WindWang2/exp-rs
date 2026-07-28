# 01 — Real Physical Data Range Display in HistogramWidget

**What to build:** Adapt X-axis, labels, and SpinBoxes to exact GDAL/QGIS band statistics ($\text{DataMin}, \text{DataMax}$) without clamping to 255.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Support 16-bit / 32-bit Float real data min/max in `HistogramWidget`
- [ ] Format X-axis labels and SpinBoxes with exact physical values
- [ ] Add unit tests in `tests/test_histogram_widget.cpp`
