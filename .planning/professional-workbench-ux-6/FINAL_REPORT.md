# Final report — Professional Workbench & Unified UX Foundation 6.0

Branch `feat/professional-workbench-ux-6` (7 commits on master @ 74fd0c7c),
worktree `../exp-rs-professional-workbench-ux-6`.

## Delivered

1. **P0 lifecycle hazards eliminated** (#777 #778 #779 #780): InspectorHost
   sections survive tab rebuilds (reparented, never destroyed with the
   QTabWidget); SelectionContext guards all sources with QPointer, purges
   doomed layers on layerWillBeRemoved with tombstones, and re-validates
   cached pointers; canvas layer destruction settles rendering first via the
   new vendored `QgsMapCanvas::stopRenderingAndSettle()` (blocking cancel of
   the main job, preview jobs and pending refreshes). Pinned by re-selection
   and mid-render-removal tests.
2. **Rendering/view isolation** (#793 #796): `viewLayerTree(viewId)` exposes
   per-view layer trees; refreshCanvasLayers drives only the owning view's
   canvas; async render-removal race pinned in two suites.
3. **Concurrency seams hardened** (#797 #798 #799 #800): bounded RsScanPool
   with generation-based cooperative cancellation for ROI/histogram GDAL
   scans; JobEngine transient capacity prevents worker-blocking sub-job
   deadlock, submitWithId enables TaskCenter pre-registration (no Dispatching
   stranding); DataManager const readers enforce the affinity contract
   (warning-level, with documented sanctioned readers).
4. **CommandRegistry migration** (#792 #794 #795): per-command shortcut
   ownership (no process aborts), installShortcut tests, multi-source
   shortcut-conflict scans.
5. **Workbench lifecycle unification** (#813): IWorkbench carries
   dirty/in-flight/cancel/close; classification, both georeferencer shells
   and OBIA wire the full hook set.
6. **ContextRules 2.0**: prerequisiteFacts projection + deterministic
   reasons for every availability-bearing command family incl. the real edit
   commands (review L).
7. **Inspector 2.0** (#812): currentChanged populates the shown tab;
   sections survive re-selection; new Vector (bounded field list, counts,
   editability) and SAR (tag-stripped whitelisted metadata facts) sections.
8. **SchemaFormBuilder 3.0**: x-ui-unit / x-ui-recommended /
   x-ui-visible-when (conditional fields excluded from values()/validate())
   / x-ui-soft-min/max (non-blocking scientific warnings with visible
   content).
9. **Docs**: docs/ui-architecture.md §12 documents every new contract.

## Test evidence (Release, offscreen, sequential, -j2 build)

All 15 in-scope test executables pass; ~120 test cases across the workbench,
selection, command, shortcut, layer-sync, display-manager, active-view,
schema-form, scan-pool, job-engine, task-center, reap, palette, workbench,
session suites. test_task_center: 31/31 cases pass in isolation; the
full-sequence admission/RSS cases are timing-flaky on this Windows host —
proven PRE-EXISTING by an A/B run with this track's TaskCenter change
stashed (identical failure lines 800/990/1079/1171/1383 + identical hang).
Scale evidence: vendored QGIS core+gui + 6 sicnu libs + 15 test targets
built locally at -j2; no parallel full-QGIS builds; vcpkg deps shared from
the prebuilt install.

## Adversarial review (Milestone L)

Two subagents (the track maximum), 23 verified findings (3+2 P1, 6+6 P2,
9 P3). ALL P0/P1/P2 resolved in commit 32ac511e (+ dba4c5bf); P3s are
documented tradeoffs or listed below. Full mapping in REVIEW_LOG.md.

## Known limitations / deferred

- App-level quit wiring that consumes the new bench
  requestCancel()/requestClose() hooks (closeEvent policy) is deferred — the
  hooks exist, are pinned by tests, and the classification lab's own
  closeEvent already confirms unsaved state; routing the window closeEvent
  through WorkbenchHost is mechanical follow-up.
- DataManager affinity remains warning-level until temporal operator
  readers migrate to submission-time snapshots (task_center #726 pattern).
- Provenance/processing-history inspector sections and per-widget
  persistence: not started (require service injection into InspectorHost).
- Ribbon/menu IA normalization beyond registry projection: current surface
  is consistent; a deeper decorative pass was deliberately out of scope
  (goal prioritizes IA/state clarity).
- test_shortcut_conflicts scans per-file; dormant cross-source duplicates
  (registry vs menu host for the same standard sequences) remain documented
  in the test.
