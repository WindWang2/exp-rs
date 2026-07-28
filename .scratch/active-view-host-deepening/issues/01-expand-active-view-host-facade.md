# 01 — Expand `ActiveViewHost` Facade Seam

**What to build:**
Deepen `ActiveViewHost` into a single GIS shell facade by adding `mapCanvasExtent()`, `mapCanvasScale()`, `messageBar()`, `setMessageBar()`, and `pushMessageBarAlert(title, text, level)`.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Add `setMessageBar(QgsMessageBar*)` and `messageBar() const` to `ActiveViewHost`
- [ ] Add `pushMessageBarAlert(const QString&, const QString&, Qgis::MessageLevel)` to `ActiveViewHost`
- [ ] Add `mapCanvasExtent() const` and `mapCanvasScale() const` to `ActiveViewHost`
