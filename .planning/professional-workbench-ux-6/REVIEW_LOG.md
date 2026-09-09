# Review log — Professional Workbench UX 6.0

## Pass 0 — baseline self-audit (before fixes)

All 15 in-scope issues confirmed at their cited locations with root causes
(see ISSUE_MAP.md). No issue was stale.

## In-flight corrections caught by the track's own tests

- InspectorHost: the first addTab on a fresh QTabWidget emits currentChanged
  (-1→0) — the shown section populated twice per rebuild. Fixed with an
  m_rebuilding guard (only user-driven switches populate).
- test_workbench_host lifecycle case: window-getter liveness flag not set —
  test bug, fixed.
- test_selection_context "removal purges": the fixture declared the
  QgsLayerTreeView BEFORE its QgsLayerTreeModel, so the model was destroyed
  first and ~QgsLayerTreeView touched the half-destructed model (null-vtable
  SEGV, diagnosed with cdb). Declaration order fixed; also fixed the batch
  runner that misreported negative crash exit codes as PASS, and hardened
  SelectionContext::attachLayerTree against a null selection model.

## Pass L — adversarial review (2 subagents, the track maximum)

Reviewer 1 — Qt/QGIS ownership, async rendering, thread-affinity, races.
Reviewer 2 — command consistency, workbench lifecycle, schema UI, a11y.

### Reviewer 1 findings → resolution (commit 32ac511e)

| Sev | Finding | Resolution |
|-----|---------|------------|
| P1 | stopRenderingAndSettle leaves PREVIEW jobs running (painter threads still read layers) | settled with the same blocking idiom; preview timer stopped |
| P1 | Overview canvas renders the SAME layer instances and was never settled | QgsMapOverviewCanvas::stopRenderingAndSettle + QgisDisplayManager::setOverviewCanvas wired from the main window; settleRendering covers both |
| P1 | #800 Q_ASSERT aborts sanctioned worker-thread temporal readers; temporalCollection accessors uncovered | enforcement downgraded to loud warning (documented sanctioned readers + migration path); temporalCollection(s) now checked; test runs in all lanes |
| P2 | settle ignores a pending scheduled refresh (timer would render freed layers) | settle stops mRefreshTimer, clears mRefreshScheduled/mRefreshAfterJob |
| P2 | #798 transient capacity leaks when a transient-backed job is cancelled while queued | cancel() Queued branch releases the slot like finishJobLocked |
| P2 | refreshCanvasLayers poured the active secondary view's cloned layers into the MAIN canvas | host now only drives the view that owns the canvas; test pins untouched main canvas + secondary canvas content |
| P3 | orphan m_taskByJobId on cancel-while-submit-in-flight | mapping removed in that branch |
| P3 | settle is sender-unsafe inside canvas render callbacks | documented on the method |
| P3 | scan pool global generations / clear-at-1024 semantics | documented tradeoff (already in header); no code change |

### Reviewer 2 findings → resolution (commits dba4c5bf + 32ac511e)

| Sev | Finding | Resolution |
|-----|---------|------------|
| P1 | layer.toggleEditing/saveEdits/attributeTable had EMPTY reasons (dead layer.edit.* case) | reason chain covers the real ids and every disabled state; tests added |
| P1 | new lifecycle test asserted an impossible state | test fixed (window liveness flag) — landed in dba4c5bf |
| P2 | setValues leaves condHidden stale | updateConditionalVisibility() called from setValues; test drives setValues |
| P2 | soft-range warning content unreachable | summary line shows the first warning verbatim |
| P2 | error→fix cycle destroyed the recommended tooltip | shared tooltipFor(field) helper used by rebuild and mark-restore |
| P2 | chained .arg() placeholder injection from layer-provided strings | single-pass multi-arg in the new inspector sections |
| P2 | SAR section rendered escaped HTML; key match case-fragile | tags stripped via QTextDocument; case-insensitive matching |
| P2 | workbench.* commands desynced the active-bench projection | routed through WorkbenchHost::activate (incl. project.newLayout) |
| P3 | unreachable m_shortcutOwners warn branch | removed |
| P3 | requestClose/requestCancel write-only; classify closeEvent guards ROI-dirty but not in-flight compute | hooks remain the contract surface (tests pin them); app-level close policy wiring deferred — see FINAL_REPORT known limitations |
| P3 | rsLazyPopulated write-only | kept (pre-existing convention, harmless) |
| P3 | conflict scan is per-file (misses dormant cross-source claims) | documented in the test; registry registration remains the uniqueness authority |
| P3 | inspector rebuild double-populate | already fixed in dba4c5bf (m_rebuilding) |
| P3 | dying-layer tombstone address-recycle tradeoff | documented; window bounded to canvas-current-layer staleness |

## Verdict after resolutions

No unresolved P0/P1. All P2 findings from both reviewers fixed with tests
where applicable. Remaining P3 items are documented tradeoffs or deferred
wiring (noted in FINAL_REPORT known limitations).
