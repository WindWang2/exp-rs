# CAPABILITY_MATRIX — linked-visual-analytics-11

| Capability | Before (a5b11b7f) | After (this PR) |
|---|---|---|
| N-view linked extent (same CRS) | controller exists, unmounted | **implemented+mounted**: per-group propagation |
| N-view linked extent (cross CRS) | transformBoundingBox in controller | implemented + independent known-answer test (EPSG:4326↔3857) |
| Per-view link groups | single global linked set | **implemented**: named groups; propagation scoped to group |
| Viewport history/undo | none | **implemented**: bounded per-view history + restorePreviousViewport |
| Extent detach lifecycle | viewAboutToBeRemoved auto-detach | kept + tested; project reset covered |
| Map↔map cursor sync | none | **implemented**: xyCoordinates → cross-CRS → peer crosshair (vertex marker), cleared on Leave |
| Chart hover (map→chart readout) | none | **implemented**: cursorProjected + VaCursorProbe async sampling, stale-generation drop |
| Chart→map marker | none | **implemented**: scatter pick → marker on active view |
| Layer visibility link | none (no visibility signal at all) | **implemented**: by AssetId across views via new manager seam |
| Layer opacity link | none | **implemented** (same seam; opacity sync when provider exposes it) |
| Layer time/frame link | none | **not supported** (would invent a second temporal authority; D9) — follow-up |
| VA selection hub | absent (comment only) | **implemented**: VaSelectionHub, typed events, generation, echo suppression, bounded history |
| Chart↔chart brushing | histogram→scatter private filter | **implemented** via hub (hist↔scatter↔categories), bounded payloads, truthful `truncated` |
| Table brushing | none | **not supported** (no hub-facing table selection signal; D8) — follow-up |
| view.* commands | none | **implemented**: linkCenter/linkScale/linkCursor/linkVisibility/linkUndo/linkGroupStatus + help entries |
| Scale sync | per-controller flag | kept, group-scoped |
| Resource bounds | payloads bounded by contract | hub history capped (64), events bounded, probe throttled (120 ms dwell, 1×1 sample) |
