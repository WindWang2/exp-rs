# 01 — Viewport Snapshot API & Extent Encapsulation in ActiveViewHost

**What to build:** Add `ViewportSnapshot` struct and `viewportSnapshot()` method to `ActiveViewHost`. Encapsulate map canvas extent, scale, CRS, and active layer name into a clean snapshot struct.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] `ViewportSnapshot` struct defined with extent, scale, crsAuthId, and activeLayerName
- [ ] `ActiveViewHost::viewportSnapshot()` returns a complete snapshot of the active view
- [ ] Code builds without warnings
- [ ] Existing `ActiveViewHost` tests pass cleanly
