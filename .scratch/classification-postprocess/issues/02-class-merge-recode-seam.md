# 02 — Class Merging & Recode Asynchronous Seam

**What to build:** Enable multi-row selection in `ClassTableWidget` to merge selected sub-classes into a single target category via `RsPostProcess::recode` executed asynchronously through `TaskCenter`, creating a new GeoTIFF without mutating the original input raster.

**Blocked by:** 01 — Class Rename & Recolor Interaction Design.

**Status:** ready-for-agent

- [ ] Multi-row selection in `ClassTableWidget` activates "Merge Selected Classes" action.
- [ ] Target class prompt allows choosing destination class ID and name.
- [ ] Asynchronous task execution runs `RsPostProcess::recode` via `TaskCenter` and registers the output raster in `DataManager`.
