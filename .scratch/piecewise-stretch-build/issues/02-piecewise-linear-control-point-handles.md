# 02 — Piecewise Linear Control Point Handles in HistogramWidget

**What to build:** Add `m_piecewisePoints` to `HistogramWidget`, render circular node handles ($\bigcirc$) connected by linear segment lines, handle left-click drag, double-click add, and right-click delete.

**Blocked by:** 01 — Real Physical Data Range Display in HistogramWidget

**Status:** ready-for-agent

- [ ] Add `m_piecewisePoints` vector to `HistogramWidget`
- [ ] Render control points and connecting linear segments overlaid on histogram
- [ ] Implement mouse drag, double-click to add, and right-click to delete
- [ ] Add signal `piecewisePointsChanged` and live map canvas renderer updates
