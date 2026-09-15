# Linked Visual Analytics & Multi-View GIS 11.0

**Branch:** `zcode/linked-visual-analytics-11` · **Baseline:** `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` (origin/master at start, 2026-09-16) · `Local evidence only; no online CI dependency`

## Mission

地图/图表/多视图选择、光标、范围与图层可见性联动 — assembled from the
Workbench 10.0 platform seams instead of re-implementing any of them:
`ViewLinkController` (existed, never mounted), `VaDataSource/VaChartWidget/
VaWorkbenchPanel`, `QgisDisplayManager` view authority, `CommandRegistry`,
`RsScanPool` generation tokens, `data/help/commands.json` help contract.

## What was already there vs. delivered (dedupe)

Phase 0 audited master fresh: the 10.0 platform had typed bounded payloads,
async sources, chart hosts with brush signals, and an N-view extent
controller with throttle/reentrancy/detach — but **no shell mount, no link
groups, no viewport history, no cursor link, no visibility link, no
selection hub** (only a comment naming one), and **no view.* commands**.
Open PRs at start (#1008 spectral, #1009 execution runtime) own zero files
in this track's business scope; shared files (tests/CMakeLists.txt,
src/app/CMakeLists.txt, .gitignore, CHANGELOG-untouched) are append-only.
Open issues #1001–#1007 dedupe: all out of scope (io/workflow/dataset/
georef domains); #1005's fail-closed lesson is cited in the contract doc.

## Delivered

- **`VaSelectionHub`** (WP-A): process-level typed selection bus — subjects
  carry authoritative ids (DisplayViewId / DisplayLayerId / AssetId /
  QgsFeatureId / payload index / pixel row-col / CRS-tagged coordinates);
  every event stamped with origin + monotonic generation; re-entrant
  same-(origin,generation) publishes are counted and dropped (loop
  suppression); bounded 64-entry history; subject validation (finite
  coordinates, ordered ranges, 256-char text clamp).
- **`ViewLinkController` 11.0** (WP-B/C): named link groups (group-scoped
  propagation, snap-on-join under echo guard), bounded per-view viewport
  history + step-back `restorePreviousViewport`, cross-CRS cursor link
  (fail-closed transforms → peer crosshair markers, Leave-cleared,
  canvas-owned), `cursorMoved`/`cursorLeft` signals, active-view tracking
  from `activeViewChanged`.
- **`VaCursorProbe`** (WP-C): async hover sampling over the sanctioned
  RsScanPool — newest-wins coalescing, 120 ms dwell throttle, stale
  generation drops, 1×1 read through the geospatial `RasterReader` seam
  (no layer handles off-thread), honest NoData/outside/failed states.
- **`VaLayerLinkController`** (WP-D): cross-view visibility/opacity sync
  keyed by the stable catalog AssetId (never names — fail-closed on
  unidentifiable layers); observes the layer-tree authority the display
  manager itself writes through → `src/app/display` untouched.
- **Brushing** (WP-E): `VaScatter` payloads optionally carry per-point
  raster geometry (cols/rows + geotransform + CRS WKT + source path); pure
  `filterScatterByXRange` keeps parallel arrays consistent; the panel
  publishes/consumes hub brushing, places a pick marker on the active
  view's canvas via geotransform arithmetic, and hosts the cursor readout.
- **`view.*` commands** (WP-F): linkCenter/linkScale/linkCursor/
  linkVisibility (checkable, projecting live controller flags), linkUndo,
  linkGroupStatus, linkUnlinkAll; help entries appended to
  `data/help/commands.json` (registry↔help coverage gate stays closed).
- **Shell mount** (WP-F/G): hub + controllers owned by QgisDesktopWindow;
  main view registers at setup; secondary/session views register where
  created (secondary map view, georef I2I/I2M, classify, OBIA).
- **Docs**: `docs/workbench/visual-analytics-linking.md` (contract:
  authorities, identity rules, loop suppression, failure semantics,
  bounds); `docs/ui-architecture.md` §33 + contracts list synced.

## Compatibility

Purely additive: existing `setLinked/isLinked` API preserved (default-group
facade); `VaWorkbenchPanel` ctor keeps working with one argument; no
existing behavior changed in `src/app/display`; new command ids are additive
with help coverage; payloads gained optional fields with honest defaults.

## Local evidence (summary; full log in .planning/linked-visual-analytics-11/)

- Configure (fresh build-dev, local Catch2 source, Qt 6.8.0 MSVC2022) → OK.
- Build: targeted test executables + app wiring, `-j2` hard cap
  (CMAKE_BUILD_PARALLEL_LEVEL=2), resource samples in EVIDENCE.md.
- Tests: `QT_QPA_PLATFORM=offscreen`, `ctest -j1` — targeted lanes
  `test_view_link`, `test_visual_analytics`, `test_dual_viewport_sync`
  (regression), `test_command_contract_9` (help↔registry), final key lanes
  run twice (see TEST_MATRIX.md for per-claim exits).

## Known limitations / follow-ups

- Layer time/frame sync: not supported (would invent a second temporal
  authority; needs a manager-level temporal seam) — CAPABILITY_MATRIX.
- Attribute-table row brushing: no hub-facing selection signal on that
  legacy surface yet.
- `view.linkUndo` availability refreshes with the registry snapshot cadence,
  so it can lag a fresh pan by one refresh.

## Review

Independent adversarial review + dispositions: REVIEW_LOG.md (P0=0, P1=0
required before PR creation; every finding dispositioned).
