# DECISIONS — linked-visual-analytics-11

D1. **Build on master's 10.0 seams, no re-implementation.** ViewLinkController,
VaDataSource/VaChartWidget, QgisDisplayManager, CommandRegistry, RsScanPool,
SelectionContext all exist and are tested. Rescope: WP-B upgrades the existing
controller instead of writing a second one; WP-A names the hub
`VaSelectionHub` exactly as the va_chart_widget.h comment anticipated.
The dual-viewport split-sync controller is a different surface and stays
untouched (master comment mandates separation).

D2. **Cross-view layer identity = AssetId, never names.** Each view records
its own DisplayLayerId for the same asset (addLayer is per-view; #674 dedupe
is per-view). DisplayLayerSnapshot::assetId() is the only stable cross-view
identity. Layers with an empty asset id are NOT linked (fail-closed honesty,
also mirrors open issue #1005's lesson: never silently fall back).
Alternative rejected: syncing by layer name (fragile, explicitly forbidden
by the goal), or by QgsMapLayer pointer (meaningless across views).

D3. **One minimal display-manager seam for visibility observation.**
QgisDisplayManager::setLayerVisible emits nothing, so a link controller
cannot observe visibility changes. Additive change: emit
`layerStateChanged(DisplayLayerId, bool visible, double opacity)` from the
existing setLayerVisible/addLayer paths (append-only, no behavior change).
Alternative rejected: connecting QgsLayerTreeNode signals per view tree —
would re-derive per-view bookkeeping the manager already owns and bypass the
manager's authority (batch updates, #793 view-local trees).

D4. **Hub ownership = shell-owned instance passed by reference, not a
singleton.** Process-level means "exactly one per running shell", not
"global static": QgisDesktopWindow creates it and passes it to the VA panel
and controllers; tests construct their own. Consistent with SelectionContext
wiring (main_window_workbench.cpp).

D5. **Loop suppression contract.** Hub stamps every published event with a
monotonic generation; while dispatching, a nested publish carrying the SAME
(origin, generation) pair is counted and dropped (an echo of the dispatch).
Cross-surface re-publication with a NEW origin token is a legitimate new
event (bounded by subscriber logic). Controllers additionally keep their
mApplying-style guards for canvas signal echo (extentsChanged during
setExtent), matching the proven dual-viewport pattern. Both layers are
tested; the oracle "no infinite feedback loop" is enforced at both levels.

D6. **Cursor link lives in ViewLinkController (extent+cursor per view
group), hover sampling is a separate VaCursorProbe.** Cursor propagation is
pure geometry (CRS transform, no I/O) so it belongs with viewport
coordination; raster sampling is I/O and rides the RsScanPool generation
pattern (120 ms dwell throttle, newest-generation-wins, stale drops).
Alternatives rejected: a separate cursor-only controller (splits one view
group concept across two objects), synchronous sampling on the GUI thread
(violates the async mandate).

D7. **view.* command ids are additive and help-catalog-backed.** New ids:
`view.linkCenter`, `view.linkScale`, `view.linkCursor`, `view.linkVisibility`,
`view.linkUndo`, `view.linkGroupStatus`. Contract test test_command_contract_9
requires data/help/commands.json entries for every registered id — entries are
appended in the same commit. Availability uses ContextFacts (≥1 view registered
→ commands enabled; no view → disabled with reason).

D8. **Brushing scope.** In: chart↔chart brushing through the hub
(range/category/point, client-side over bounded payloads — no rescan),
scatter-pick → map marker, map hover → panel readout, honest `truncated`
flags. Out (documented follow-up): attribute-table row brushing and
server-side region recompute — the table surface has no hub-facing selection
signal today and building one would touch out-of-scope widgets.

D9. **Temporal/frame sync (WP-D "time/frame") is scoped to what master
exposes.** QgsMapLayer temporal properties are per-layer renderer state with
no manager-level authority for cross-view temporal frames; implementing it
would invent a second temporal authority (forbidden). Delivered: visibility +
opacity sync by AssetId; time/frame listed in CAPABILITY_MATRIX as
not-supported with the seam identified for a future track.

D10. **main_window edits are minimal integration only** (member pointers,
one setup call, command handler lambdas). #991 merged so the old read-only
rule no longer applies, but #1008/#1009 own no main_window files, so the
conflict surface stays small.

D11. **Cross-CRS known answers use an independent truth**: tests compute the
expected transformed point/rect with a directly-constructed
QgsCoordinateTransform from EPSG:4326 ↔ EPSG:3857 with hand-computed values
(0,0 → 0,0; 45N,90E → 1.0007531e7, 1.0007531e7 within tolerance) — the test
does not call the controller's own transform helper.
