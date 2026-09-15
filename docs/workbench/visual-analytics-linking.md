# Linked Visual Analytics — multi-view linking contract (11.0)

Scope: 地图/图表/多视图选择、光标、范围与图层可见性联动 — how the pieces
fit, what is authoritative, and what the failure semantics are. This document
is the contract surface for `test_view_link` and `test_visual_analytics`.

## Authorities

| Concern | Authority | Notes |
|---|---|---|
| View registry / canvases | `display::QgisDisplayManager` (ProjectContext-owned) | controllers own NO canvases; everything is re-queried per event |
| Extent/cursor link | `ViewLinkController` (src/app/shell/) | per-GROUP propagation; active view tracked via `activeViewChanged` |
| Layer visibility/opacity link | `VaLayerLinkController` (src/app/visualanalytics/) | keyed by catalog **AssetId** — the only stable cross-view identity |
| Selection/brush events | `VaSelectionHub` (src/app/visualanalytics/) | process-level; exactly one per shell, shell-owned (no singleton) |
| Hover sampling | `VaCursorProbe` | RsScanPool generation contract; 1×1 read, no GUI-thread I/O |

## Identity rules

- Views are `DisplayViewId` tokens; per-view layers are `DisplayLayerId`
  records; the SAME asset displayed in two views has **two** DisplayLayerIds
  and **one** AssetId — AssetId is the link key. Layers without an asset id
  are never linked (no name guessing, fail-closed).
- Hub subjects carry the owning stores' authoritative ids
  (`object_identity.h` philosophy — the hub mints no second id space).
- Coordinates always carry their CRS WKT authority; chart coordinates are
  data coordinates (no CRS) and say so by carrying an empty one.

## Loop suppression (two layers)

1. **Hub**: every published event carries `(origin, generation)`. A publish
   re-entering during a dispatch with the SAME `(origin, generation)` is an
   echo — dropped and counted (`suppressedEchoes`), never re-broadcast.
   Cross-surface republication is fresh intent.
2. **Controllers**: `mApplying`-style guards absorb the Qt signal echo of our
   own peer mutations (`extentsChanged` during `setExtent`,
   `visibilityChanged` during `setLayerVisible`, `opacityChanged` during
   `setOpacity`).

Both layers are asserted by tests; no linking surface may bypass them.

## Failure semantics (fail-closed)

- CRS transform failure (extent, cursor, probe, pick marker): the peer is
  left UNCHANGED and a stat counter ticks (`transformFailures`). We never
  apply untransformed geometry (lesson of issue #1005).
- Probe: NoData, outside-extent and open/read failures deliver as
  `ok=false` with a human-readable reason — never as a wrong value.
- Unlink (all groups cleared) always converges to independent views.

## Resource bounds

- Hub history: bounded ring (64 events). Events are small value types; the
  100k-logical-event test asserts bounded memory via the ring cap.
- Viewport history: 16 entries per view, deduped by near-equality.
- Cursor: pointer moves are coalesced by Qt; per-event cost is O(peers)
  PROJ transforms; markers are per-view single reused canvas children.
- Probe: newest-wins coalescing + 120 ms dwell throttle; at most one queued
  sample; stale generations drop before delivery.

## Command surface

`view.linkCenter` · `view.linkScale` · `view.linkCursor` ·
`view.linkVisibility` · `view.linkUndo` · `view.linkGroupStatus` ·
`view.linkUnlinkAll` — checkable toggles project the live controller flags;
undo/uncouple operate on the display manager's active view; every command
degrades to "unavailable" (with a reason) when controllers are absent.
Help entries live in `data/help/commands.json` (registry↔help coverage is
enforced by `test_command_contract_9`).

## Deliberately not supported

- Layer **time/frame** sync: would invent a second temporal authority on top
  of the temporal workbench; revisit behind a manager-level temporal seam.
- Attribute-table row brushing: the table surface exposes no hub-facing
  selection signal today (would require touching out-of-scope widgets).
- Syncing by layer NAME: forbidden by design (see DECISIONS D2).
