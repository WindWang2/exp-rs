# R1 Adversarial Review — r22-workbench (branch agent/r22-workbench-20261025T163805)

Reviewer: R1 (fresh, no prior context). Scope: `git diff 911e0b17a3..HEAD` (10 commits).
Method: full diff read hunk-by-hunk, surrounding code read in every touched file, callers
grepped (`AsyncRunnerBase` subclasses, `removeLayers`, `jobCancelInFlight`,
`LayerTreeMenuProvider` wiring, display-manager view/layerStore topology), and all five
new test targets **built and linked** in `build-r22-workbench`; `test_r22_workbench_ui_contract`
was executed and passes (30 assertions / 5 cases). The other four binaries cannot run on this
review machine (`libarmadillo.so.15` absent) — environment limitation, not a diff defect; the
ledger claims GREEN×2 elsewhere.

## VERDICT: APPROVE-WITH-FIXES

One crash-class window introduced by the W9 escape hatch (R1-1) and one latent
null-deref in the new W4 menu lambda (R1-2) should be fixed before merge. Everything
else verified correct, including the two multi-view traps this review was asked to
hunt (see "verified non-findings").

## Findings

| ID | Severity | Location | Kind | Summary |
|----|----------|----------|------|---------|
| R1-1 | P1 | src/app/dialogs/sicnu_algorithm_dialog.cpp:474-491 (+ :205, :644) | NEW defect | Close-while-Cancelling escape hatch allows `delete mFeedback` while the worker thread is still inside `runPrepared(..., fb)` using that same feedback object |
| R1-2 | P2 | src/app/layer_tree_menu.cpp:118-126 | NEW defect | `nodeGuard->parent()` dereferenced without re-check after `removeLayers()`, which runs a modal confirmation (nested event loop) |
| R1-3 | P3 | resources/translations/sicnu_zh_CN.ts (not updated) vs src/app/layout/qgslayoutdesignerdialog.cpp, cartography_dock.cpp:149, active_view_host.cpp:488-491 | process gap | Source-text changes orphan existing zh_CN translations; no .ts regeneration in the diff |
| R1-4 | P3 | tests/CMakeLists.txt:16447-16549 | process gap | EOF append block guarantees a textual conflict with any sibling EOF append (r22-inference); hand-listed TU deps will break silently on future includes |
| R1-5 | P3 | src/app/active_view_host.cpp:475,530 + active_view_host.h:146-150 | NEW (minor) | `removeLayers()` returns true on accepted confirmation even if every per-layer removal failed; group node then removed despite failed routing |
| R1-6 | P3 | src/app/map_tools/map_tool_manager.cpp:144-156 | NEW (minor) | Computed count correct (20; old literal 19 was stale), but `kNavigationToolCount = 6` is a new hidden constant with the same drift problem one level up |

### R1-1 — P1, NEW defect: feedback use-after-free window via the new close escape hatch

`SicnuAlgorithmDialog::closeEvent` now accepts the close once the cooperative cancel is
armed:

```cpp
if ( mJobHandle.isRunning() && !sicnu::app::jobCancelInFlight( mJobHandle ) )
{ ... cancel ... event->ignore(); return; }
// Once the cooperative cancel is armed (#1560, same escape hatch as the
// raster dialogs) accept the close ...
QgsProcessingAlgorithmDialogBase::closeEvent( event );
```

The dialog is created with `dlg->setAttribute(Qt::WA_DeleteOnClose)` (src/app/
main_window_processing.cpp:141), so the accepted close destroys the dialog. The new
comment claims safety: "the worker owns its context and reads the dialog feedback
through a QPointer guard, so the task is safe to outlive this dialog." That is only
half true:

- Context: safe — `state->context = std::make_unique<QgsProcessingContext>()` is
  worker-owned (sicnu_algorithm_dialog.cpp:613-615).
- Feedback: NOT safe mid-call. The worker does

```cpp
QgsProcessingFeedback *fb = feedbackGuard.data();
state->results = state->algorithm->runPrepared( state->parameters, *state->context, fb );
```

  (sicnu_algorithm_dialog.cpp:643-645) and `runPrepared` dereferences the raw `fb`
  (`pushProgress`, `isCanceled`, `pushInfo`) for the whole execution. The QPointer
  guard protects the pre-call checks only. Meanwhile `~SicnuAlgorithmDialog` runs
  `delete mFeedback;` (sicnu_algorithm_dialog.cpp:205).

`jobCancelInFlight` requires `TaskStatus::Cancelling`, and TaskCenter documents that
queued/waiting cancels finalize synchronously to `Canceled` (task_center.cpp:483-487
comment) — so Cancelling implies the worker is dispatched and executing. The
cooperative cancel is exactly the state where the worker keeps running until its next
checkpoint; one long GDAL sub-step between `feedback->isCanceled()` checkpoints is a
multi-second window. Sequence: run → close (cancel armed) → close again → WA_DeleteOnClose
→ `delete mFeedback` → worker calls `fb->pushProgress()` → UAF/crash.

This re-opens the exact hazard the pre-diff code documented: "#339: prevent
WA_DeleteOnClose from destroying mContext/feedback while the worker still dereferences
them." The raster-dialog escape hatch this mirrors is safe because its worker lambda
captures no dialog state (`[task, errMarker]`, raster_processing_dialog_base.cpp:401);
this dialog's worker does.

Suggested fixes (any one):
1. Gate the escape hatch on the run state actually being quiescent, e.g. also require
   `!mRunState || !mRunState->workerStarted` — the workerStarted flag already exists.
2. Transfer feedback ownership into the run state (`std::shared_ptr<QgsProcessingFeedback>`
   in `state`, captured by the worker) and skip `delete mFeedback` in the destructor
   while `mRunState` is set; delete on completion instead.
3. On the Cancelling path, accept the hide but clear `WA_DeleteOnClose` and defer the
   actual deletion to the terminal callback (`algExecuted` path), which already runs on
   the GUI thread via the handle.

### R1-2 — P2, NEW defect: stale QPointer dereference after modal confirmation

```cpp
const bool removedAny = hostPtr->removeLayers( layers );
if ( layers.isEmpty() || removedAny ) {
    if ( QgsLayerTreeGroup *parent =
             qobject_cast<QgsLayerTreeGroup *>( nodeGuard->parent() ) )   // <-- no recheck
        parent->removeChildNode( nodeGuard.data() );
}
```

(layer_tree_menu.cpp:118-126). The null check `if ( !hostPtr || !nodeGuard ) return;`
runs BEFORE `removeLayers`. `removeLayers` presents the production confirmation, which
is a modal `QMessageBox::question` (active_view_host.cpp:91) — a nested event loop in
which the group node can be destroyed (project close, another view teardown, an async
removal completing). `nodeGuard` would then be null and `nodeGuard->parent()` is a
null-pointer dereference. Low probability, but this is a NEW code path. Fix:

```cpp
if ( nodeGuard && ( layers.isEmpty() || removedAny ) ) {
    if ( auto *parent = qobject_cast<QgsLayerTreeGroup *>( nodeGuard->parent() ) )
        parent->removeChildNode( nodeGuard.data() );
}
```

### R1-3 — P3, process gap: orphaned zh_CN translations

The diff changes source text of strings that have live zh_CN translations and does not
touch resources/translations/sicnu_zh_CN.ts:

- `Deleted %1 item(s)` → 已删除 %1 个项目 (sicnu_zh_CN.ts:50426) replaced by
  "Deleted 1 item"/"Deleted %1 items" — same for Duplicated/Locked/Unlocked
  (ts:50434-50454) and `Select item(s) to duplicate first.` (ts:50434).
- `Thematic composition arranged (%1 new component(s)…)` (ts:50466).
- `DPI：` → `DPI:` (ts:98268 had a zh_CN entry).

The plural `Remove the selected %1 layers…` source text is byte-identical to the old
one, so that zh_CN entry survives; only the new singular variant is untranslated. Net:
Chinese UI falls back to English in six status/message spots until a translation pass.
Acceptable for an English-punctuation normalization PR, but the .ts regeneration
follow-up should be recorded (suggest a ticket).

### R1-4 — P3, process gap: EOF append block collision + hand-listed TU deps

tests/CMakeLists.txt appends 103 lines at EOF. Any sibling track appending at EOF
(r22-inference) produces a guaranteed textual conflict for the second PR to land —
acknowledged in .planning/r22-workbench/PR_COLLISION_MATRIX.md but still a merge cost.
Also, `test_r22_workbench_view_tree` compiles `active_view_host.cpp` +
`layer_tree_menu.cpp` + three workbench TUs by hand; a future include added to any of
those files breaks this target's link with no local hint. This matches the established
repo pattern (dialog tests do the same), so accepted; noted for awareness.

### R1-5 — P3, minor NEW: `removeLayers` return semantics vs doc

Header doc: "Returns true when at least one layer removal actually ran (confirmation
accepted)." Implementation returns true whenever the confirmation was accepted, even if
every `m_displayManager->removeLayer(...)` failed (diagnostics reported). The menu
lambda then removes the group node anyway, so a fully-failed group removal still loses
its node (layers remain manager-owned but orphaned from the tree until view refresh).
Edge case; either tighten the return value or the doc.

### R1-6 — P3, minor NEW: navigation-tool count is a new hidden constant

The computed `editingToolCount` is correct (20 = 13 in-list + 7 later; the old literal
19 was stale) and self-maintaining against editing-tool additions. But
`constexpr int kNavigationToolCount = 6` re-introduces the drift problem: adding a
7th non-editing tool to `m_allTools` silently inflates the count. The `laterEditingTools`
list is also only ever used for `.size()`. Acceptable improvement over a bare literal;
flagging for the record.

## Verified non-findings (checked, not defective)

1. **W1 destructor/endRun interleavings (checklist 4)** — every interleaving is safe:
   - Lambda ran first → `m_running=false`, `m_cursorOverrideActive=false` → base dtor
     calls nothing. No double `restoreOverrideCursor`.
   - Dtor while running (both subclasses disconnect their completion paths first:
     AsyncGdalRunner disconnects the watcher, AsyncAlgorithmRunner disconnects the task)
     → `m_running` still true → base dtor `endRun()` restores exactly once via the
     push flag.
   - "Task finishes between derived body and base dtor" cannot happen (single GUI
     thread, connections severed under the derived dtor); posted metacalls targeting a
     dying QObject are purged by ~QObject.
   - `m_cursorOverrideActive` (push flag, not parent liveness) is the correct key: a
     parent destroyed between construction and `run()` means no push and no pop.
   - `m_hadParentWidget` fixes null-parent delivery and does not invert the
     dead-parent suppression (`m_hadParentWidget && !dialogGuard`). No production
     callers exist for either deprecated runner (grep: tests only), so no caller breakage.
2. **W4 cross-view trap (checklist 3)** — `LayerTreeMenuProvider` is wired only to the
   main window's tree (main_window_connections.cpp:224-225). The main view's spec is
   `QgsProject::layerTreeRoot()/layerStore()` (main_window.cpp:128-132), so both
   branches of the removal loop (display-manager route via
   `DisplayLayerId::fromMapLayer` → `removeLayer` which uses the layer's OWN
   `viewRecord` — qgis_display_manager.cpp:946-975 — and the legacy
   `QgsProject::removeMapLayer` route) address the same store. Secondary views have
   their own tree/store (secondary_map_view_widget.cpp:64-73) and never appear in the
   main tree, so a secondary-view group cannot be removed through this menu. A plain
   (non-display) user layer in a main-tree group takes the project-store branch —
   correct. The new test fixture deliberately uses a NON-project `QgsMapLayerStore`
   and proves the display-manager branch releases the lease (leaseCount 1→0) with no
   ghost records — exactly the trap scenario.
3. **W2 page routing (checklist 6)** — counted `m_paramStack->addWidget` calls in
   setupUi order: 0 summary, 1 composite, 2 index, 3 trend (hint-only), 4 anomaly,
   5 extract-series, 6 seasonal breaks, 7 model select, 8 phenology — matches
   `kAlgorithms` order and the three new `algorithmChanged` branches. Combo items
   carry the id as itemData (temporal_analysis_dialog.cpp:151), so `findData` routing
   is correct. Test markers (`temporalSeasonalHarmonics`, `temporalSelectPenalty`,
   `temporalPhenoCycles`, …) all exist and the test checks the marker is on the
   CURRENT page — not tautological.
4. **W3 (checklist 7)** — same-CRS path skips the transform (`srcCrs != canvasCrs`
   guard), byte-equivalent to old behavior. `QgsCsException` early-return leaves the
   extent unchanged and logs; the subsequent `refresh()` is a harmless no-op redraw
   — strictly better than the old always-setExtent which wrote layer-CRS units into a
   foreign CRS canvas. `zoomToLayer` and two other callers keep using the preserved
   `setCanvasExtentReprojected` wrapper; no other callers of the old helper existed.
5. **W5 edge cases (checklist 5)** — typed "All" while index 0 is current deactivates
   the filter (acceptable; a literal "All" tag still filters when selected as an item
   at index > 0); cleared text on a tag item shows everything because
   `tag.contains("")` is true — identical outcome to the old code for that state (old
   also did not filter). No regression; the fixed duplicate `tagText != tr("All")
   && tagText != tr("All")` clause and `itemText(0)` comparison are more robust than
   the old translation-sensitive literal.
6. **W9 move** — old anonymous-namespace `jobCancelInFlight` had no other users; all
   three call sites now use `sicnu::app::jobCancelInFlight`; ui-contract test pins
   both header declaration and dialog use. `~GuiJobHandle` nulls callbacks before the
   object dies, so no dangling completion callbacks fire after dialog destruction
   (only R1-1's raw `fb` inside `runPrepared` is exposed).
7. **Batch external cancel** — the Canceled branch is the only
   `TaskStatus::Canceled` occurrence in the file, so the 700-char contract window
   pins the right branch (not vacuous); setting `m_canceled` makes external
   TaskCenter cancels stop the chain exactly like the Stop button (which sets it at
   line 393), and the canceled item is counted in neither success nor fail —
   consistent with the pre-existing Stop path.
8. **W6** — `deleteLater` + immediate `m_importCenter = nullptr`; every other use is
   null-guarded; only the `finished→refresh` connection existed and is disconnected
   first. Clean.
9. **Tests/build** — all five targets compile and link in build-r22-workbench
   (verified by building them). `Qt6::Concurrent` linked where needed (async runner
   target and via `sicnu_add_dialog_test`); the temporal test's `NO_DEFAULT_MAIN`
   correctly switches to `Catch2::Catch2` (own main with QgsApplication);
   SicnuUi symbols come from `dialog_utils.cpp`, which the dialog-test helper always
   compiles in. `test_r22_workbench_ui_contract` executed here: pass. The
   source-text-coupled contract tests are brittle by design and declared as
   tripwires — acceptable; their assertions pin real branches (checked for the
   batch/W6/W9/map-tools cases).
10. **Whitelist (checklist 11)** — every changed path is under `src/app/**` (none in
    teaching_admin), `tests/test_r22_workbench_*`, `tests/CMakeLists.txt`, or
    `.planning/r22-workbench/**`. Compliant. No files outside the whitelist.
