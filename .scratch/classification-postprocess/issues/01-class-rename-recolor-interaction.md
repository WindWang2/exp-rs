# 01 — Class Rename & Recolor Interaction Design

**What to build:** Allow users to double-click class labels in `ClassTableWidget` to rename them (e.g. 1 -> "Water") and edit RGB color swatches, updating the QGIS map canvas palette (`QgsPalettedRasterRenderer`) in real-time and saving metadata to `<raster_name>.class.json`.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Double-clicking class ID or name in `ClassTableWidget` opens in-place editor.
- [ ] Changing a color swatch emits palette update signal to `QgisDisplayManager`.
- [ ] Saving/loading a project serializes and restores class definitions from `<raster_name>.class.json`.
