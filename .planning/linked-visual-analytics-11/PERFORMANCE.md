# PERFORMANCE — linked-visual-analytics-11

Resource model (correctness never gated on wall-clock):

- **Selection hub**: bounded by construction — history ring capped at 64
  events; every event is a small value type (ids + one optional point/rect);
  dispatch is synchronous signal fan-out to direct connections. Scale
  evidence: 100k logical publishes with subscribers attached complete with
  history capped at 64 and per-subscriber receipt == 100k (invariant, not
  timing). No queues grow with event count.
- **Extent link**: 16 ms single-shot throttle coalesces pan/zoom storms
  (same class as master's dual-viewport sync); one propagation pass per
  throttle fire; reentrancy guard prevents recursive passes. Per-pass cost
  is O(linked peers) transforms (bounded by views, typically ≤ 4).
- **Cursor link**: pointer-move events coalesced by Qt event compression;
  per-event cost = O(peers) CRS point transforms (PROJ, µs class); NO I/O.
  Crosshair markers are per-view single QgsVertexMarker (reused, never
  allocated per event).
- **Hover probe (VaCursorProbe)**: dwell-throttled (120 ms) → ≤ ~8 in-flight
  generations/sec worst case; newest-generation-wins; at most ONE queued job
  per probe (supersede cancels logically; worker polls stale flag); sample
  is a 1×1 read. No accumulation: results older than newest generation are
  dropped before delivery.
- **Visibility link**: event per visibility change (user-rate); per-event
  O(peers sharing the asset).
- **Build/test budget**: `-j2` build / `-j1` tests; CPU+RSS sampled during
  builds (EVIDENCE.md). Tests are offscreen; the 100k-event test is the
  logical-scale gate and stays in the default suite only if it remains
  sub-second; otherwise it moves behind a label without losing the
  bounded-history invariant assertions.
