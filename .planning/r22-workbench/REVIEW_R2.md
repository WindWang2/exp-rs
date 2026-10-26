# R2 Adversarial Review — r22-workbench (branch agent/r22-workbench-20261025T163805)

Reviewer: R2 (fresh; R1's REVIEW_R1.md read only to know what to attack — every conclusion below was
re-derived from the code, and the two late-commit gates were re-traced from scratch).

Scope: `git diff 911e0b17a3..HEAD` (13 commits). Charter: (1) independently re-derive the risk of the
three R1-driven late changes in d8d99ee28d; (2) adversarial scenarios per W1–W9; (3) RED/vacuity audit of
the five new test binaries; (4) hygiene; (5) whitelist.

## VERDICT: APPROVE-WITH-FIXES

No P1. The late R1-fix commit is sound in the non-race interleavings I constructed (each traced below),
and I found no reachable *deterministic* crash introduced by HEAD. Two P2s remain:

- **R2-1 (NEW)** — the `workerStarted` gate added by the late commit is a cross-thread read of a plain
  `bool`: formally a data race, and its stale-read window re-opens exactly the R1-1 UAF it was added to
  close. One-line fix (`std::atomic<bool>`).
- **R2-2 (pre-existing, in-scope of W9)** — `reject()` (Esc key / button-box Close) bypasses the
  hardened `closeEvent` entirely and deletes the `WA_DeleteOnClose` dialog mid-run — the same feedback
  UAF, reachable without any race.

Everything else I attacked held: W1–W8 fixes traced correct (including the modal node-guard and the
derived tool count, both re-derived from scratch), all five test binaries pass on HEAD, the ui_contract
binary independently re-derived RED on pre-fix sources (12 failed assertions), no vacuous behavioral
assertions, no debug leftovers, whitelist clean.

## Findings

| ID | Severity | Location | Evidence (quoted) | Kind | Suggested fix |
|----|----------|----------|-------------------|------|---------------|
| R2-1 | P2 | src/app/dialogs/sicnu_algorithm_dialog.cpp:492 (read) vs :649 (write); type at sicnu_algorithm_dialog.h:46 | GUI: `if ( mRunState && mRunState->workerStarted )` / worker: `state->workerStarted = true;` — plain `bool workerStarted = false;` | NEW (introduced by d8d99ee28d) | Make it `std::atomic<bool>` (relaxed store/load suffices; the value only gates a policy decision). See analysis. |
| R2-2 | P2 | src/app/dialogs/sicnu_algorithm_dialog.cpp (missing `reject()` override); creation site src/app/main_window_processing.cpp:140 `dlg->setAttribute(Qt::WA_DeleteOnClose);` | No `reject` override in sicnu_algorithm_dialog.h; base `QgsProcessingAlgorithmDialogBase::reject()` only *sets* the attribute when finalized, never clears it | pre-existing (both before and after this PR) but squarely inside W9's claimed protection | Override `reject()` with the same gate as `closeEvent` (or clear `WA_DeleteOnClose` while `mJobHandle.isRunning()`), mirroring `RasterProcessingDialogBase::reject()`. |
| R2-3 | P3 | src/app/active_view_host.h:146-150 / layer_tree_menu.cpp:118 | `hostPtr->removeLayers( layers );` — return value dropped; `removeSelectedDisplayLayers()` also ignores it | NEW (minor, from d8d99ee28d's R1-5 rework) | Either `[[nodiscard]]` it and use it, or drop the return; currently zero users. |
| R2-4 | P3 | tests/test_r22_workbench_ui_contract.cpp:107-119 | `CHECK( body.contains( QStringLiteral( "workerStarted" ) ) );` | process | The W9 contract pins token presence, not semantics — R2-1's race (or an inverted gate) passes this test. Acknowledged tripwire pattern, but the toolbox close/reject lifecycle has no behavioral test; record a follow-up seam (e.g. a testable `canCloseNow()` predicate). |
| R2-5 | P4 | src/app/shell/gui_job_adapter.cpp:156 | `sicnu::TaskCenter::instance().getTaskInfo( taskId )` while `GuiJobHandle` may be constructed with an injected center (gui_job_adapter.cpp:62-64) | NEW (moved code) | Latent inconsistency only (production always uses the default center). Pass the center through, or assert sameness. |
| R2-6 | P3 (process) | .planning/r22-workbench/LOOP_LEDGER.md R4; /tmp artifacts | ledger claims RED counts ("view_tree W3 :130 红…"), but no test-run output was persisted (only r22wb-build-*.log build logs exist in /tmp) | process | RED for 4 of 5 binaries is asserted, not evidenced. I independently re-derived RED for ui_contract (below); the other four I verified by code-trace only (rebuild-with-revert not run on this machine for time). Persist run logs next time. |

### R2-1 — P2, NEW: `workerStarted` gate is a data race that can re-open the R1-1 UAF

Re-derivation (no R1 inheritance): `SicnuProcessingRunState::workerStarted` is a plain `bool`
(sicnu_algorithm_dialog.h:46). The worker-thread executor stores it
(sicnu_algorithm_dialog.cpp:649) *immediately before* the `!feedbackGuard` check and the
`runPrepared(..., fb)` call that dereferences the dialog-owned feedback raw for the whole run
(:662-664). The GUI thread reads it in `closeEvent` (:492) with no synchronization — no mutex, no
queued signal, no atomic. Under the C++ memory model this is UB; on real hardware the practical hazard
is a **stale-false read**:

1. Run starts; worker dispatches and executes `state->workerStarted = true;` (store not yet visible).
2. User closed once (cancel armed → `TaskStatus::Cancelling`), closes again.
3. GUI reads `workerStarted == false` (stale) → falls through → `QgsProcessingAlgorithmDialogBase::closeEvent`.
4. The dialog was created with `WA_DeleteOnClose` (main_window_processing.cpp:140), so the accepted
   close deletes it (the base class's own conditional `deleteLater()` at
   qgsprocessingalgorithmdialogbase.cpp:735-740 is *not* the deletion mechanism — Qt's close machinery
   honors the attribute unconditionally).
5. `~SicnuAlgorithmDialog` runs `delete mFeedback;` (:205) while the worker is entering/inside
   `runPrepared` with the raw `fb` → use-after-free. Exactly the R1-1 crash, narrowed to the
   store-visibility window.

The *non-race* interleavings of the same gate are correct — I traced all of them:

- **Cancelling + worker genuinely not started** (task dispatched to a busy pool; executor body not yet
  entered; `TaskStatus::Cancelling` persists because per task_center.cpp:482-484 "dispatched ones go
  Cancelling and resolve via the terminal record"): close accepted → dialog deleted → feedback
  QPointer-nulls → executor's first check `feedbackGuard && feedbackGuard->isCanceled()` short-circuits
  false → `ctx.throwIfCancelled()` throws (engine cancel was armed), and if that were ever missed the
  NEW guard `if ( !feedbackGuard ) throw ... "Dialog closed before execution started"` (:654-657)
  catches it *before* `runPrepared`. Safe, and the guard is load-bearing, not dead code.
- **Cancelling + workerStarted=true visible**: close refused (`event->ignore()`) until the terminal
  record lands. No livelock found: `isRunning()` goes false as soon as the terminal `Canceled` record
  arrives (gui_job_adapter.cpp:234), which the armed cancel guarantees.
- **mRunState reset timing**: `mRunState.reset()` runs only inside the success/failure callbacks, which
  run on the GUI thread *after* `m_taskId = -1` (gui_job_adapter.cpp:234-239) — i.e. after
  `isRunning()` is already false — so `closeEvent` can never observe a null `mRunState` while running;
  the `mRunState &&` check is defensive only. No contradiction.
- **Deletion-order**: in `~SicnuAlgorithmDialog`, `mJobHandle.cancel()` (:196) runs *before*
  `delete mFeedback` (:205), so the cancel callback (`feedbackGuard->cancel()`) always touches a live
  feedback; the re-cancel from `~GuiJobHandle` fires when the guard is already null and is guarded.

Only the visibility race breaks the argument. Fix is one line: `std::atomic<bool> workerStarted{false};`
(reads/writes stay plain syntax).

### R2-2 — P2, pre-existing but in-scope: Esc / button-box Close deletes the dialog mid-run

`SicnuAlgorithmDialog` overrides `closeEvent` only. `QDialog::done()` (reached from Esc via
`keyPressEvent`→`reject()`, and from the button box via `closeClicked()` → `reject()` at
qgsprocessingalgorithmdialogbase.cpp:588-592) hides and deletes a `WA_DeleteOnClose` dialog **without
sending a QCloseEvent** — the attribute is set at creation (main_window_processing.cpp:140), and the
base `reject()`'s `if ( !mAlgorithmTask && isFinalized() ) setAttribute(...)` (qgsprocessingalgorithmdialogbase.cpp:949-955)
only ever *sets* it, never clears it while a job runs. `blockControlsWhileRunning()` disables
`mButtonRun`/`mButtonChangeParameters`/`mMainWidget`, not the button box. So: run a long algorithm →
press Esc → dialog deleted → `delete mFeedback` → worker inside `runPrepared` with raw `fb` → UAF.
Pre-existing (identical structure at 911e0b17a3), but it defeats the very hazard the diff's own #339
comments claim is handled ("prevent WA_DeleteOnClose from destroying mContext/feedback while the worker
still dereferences them" — true only for the window-manager close path), and the PR's charter is the
toolbox close defect class. Suggested fix mirrors `RasterProcessingDialogBase::reject()`
(raster_processing_dialog_base.cpp:76-82).

### Verified non-findings (independently re-derived, with the adversarial scenario)

1. **Executor pre-run guard vs parent destruction** (charter Q: "dialog deleted by parent destruction
   mid-run — does the executor throw Cancelled where old code crashed or proceeded?"). Parent teardown
   mid-run while the worker is PAST `workerStarted=true` → UAF (pre-existing #339, unchanged, not
   worsened). Worker NOT yet past it: old code called `runPrepared(params, ctx, nullptr)` (nullptr
   feedback deref inside QGIS); new code throws `ErrorCode::Cancelled` — a strict improvement. There is
   no path where `mFeedback` dies before the cancel is armed: `~SicnuAlgorithmDialog` arms cancel iff
   `isRunning()` (:194-197); `algExecuted` deletes only after the terminal callback; `runAlgorithm`
   prepare-fail paths delete only when no job was submitted.
2. **Node-guard after modal (R1-2 fix)** — re-derived: `m_confirmationFn` defaults to a modal
   `QMessageBox::question` (active_view_host.cpp:88-91), a nested event loop in which the tree can be
   torn down. The lambda re-checks `if ( nodeGuard )` *after* `removeLayers` and re-fetches
   `nodeGuard.data()` (layer_tree_menu.cpp:125-126); the pre-modal `static_cast` is safe because
   `nodeType()` was checked at menu creation and node types are immutable. Declining the confirmation
   → layers survive → `findLayers()` non-empty → group kept (correct). Empty group → `removeLayers({})`
   early-returns false without prompting → `findLayers()` empty → group removed silently (matches stock
   QGIS). Mixed broken/null `layer()` nodes are excluded from `layers` and keep `findLayers()` non-empty
   → group kept — conservative, acceptable. Nested subgroups: `findLayers()` is recursive and
   `removeChildNode` removes the subtree — no orphaned subgroups. Root parent: the project root *is* a
   `QgsLayerTreeGroup`, so the `qobject_cast` succeeds for top-level groups. No defect found.
3. **Derived navigation count (R1-6 rework)** — re-derived from the member lists
   (map_tool_manager.cpp:128-157): `m_allTools` = 6 navigation + 13 editing = 19; `laterEditingTools` =
   exactly the 7 post-freeze creations (all absent from `m_allTools`); `navigationTools` = exactly the
   first 6 `m_allTools` entries → 19 − 6 + 7 = **20**, self-consistent with the real inventory (the old
   literal 19 was stale). `m_selectTool` is counted as "editing" — a semantic nit, not a drift risk.
4. **W1 async runner lifecycle** — all destructor interleavings traced against both subclasses:
   derived dtors sever connections before the base dtor runs `endRun()`, which is keyed on
   `m_cursorOverrideActive` (push flag) rather than parent liveness — a parent destroyed between
   construction and `run()` pushes nothing and pops nothing. `m_hadParentWidget && !dialogGuard`
   (async_gdal_runner.cpp:42) suppresses delivery only for a *dead former parent*; a never-parented
   runner delivers — the #1695 null-parent swallow, fixed. Verified live: all 4 async test cases pass,
   including the parent-death SIGSEGV case that was red pre-fix per ledger.
5. **W2 temporal pages** — re-counted `m_paramStack->addWidget` order against `kAlgorithms` order:
   0 summary … 8 phenology; the three new branches (6/7/8) land on the pages that build
   `temporalSeasonalHarmonics`/`temporalSelectPenalty`/`temporalPhenoCycles`. Verified live: 30/30.
6. **W3 zoom native CRS** — `setCanvasExtentTransformed` reuses the exact transform/failure contract of
   `zoomToLayer` (CsException → leave extent, warn); same-CRS path byte-equivalent to old behavior.
   Verified live (70/70 with W4; needs `PROJ_LIB` on this machine — environment, not code).
7. **W4 group Remove** — verified live: lease released (1→0), no ghost view records, group node gone,
   via the real menu provider + real `QgisDisplayManager`/`DataManager` fixture.
8. **W5 log tag filter** — adversarial states traced: typed-partial at index 0 (fixed), item-selected,
   cleared text on an item (`contains("")` → shows all, same as old), literal "All" tag, empty combo.
   Verified live: 24/24.
9. **W4(batch) external cancel** — `TaskStatus::Canceled` arm `m_canceled = true;`
   (batch_processing_dialog.cpp:548) stops the chain through the pre-existing `submitNextBatchItem`
   gate (:487); the dialog's own Stop path is idempotent under it; stale records for superseded
   `m_batchTaskId` are filtered by the `info.taskId != m_batchTaskId` guard (:534).
10. **W6/W7/W8** — `deleteLater` after `disconnect` only (workspace_browser_panel.cpp:347-349);
    DPI colon; all four plural sites branch on `== 1`. Contract tests pin all three (red pre-fix —
    proven below).

## Independent verification log

Commands run on this machine (worktree `/home/kevin/project/exp-rs-r22-workbench-20261025T163805`):

1. `git log --oneline 911e0b17a3..HEAD` → 13 commits, `git diff --name-only 911e0b17a3..HEAD` → 23
   files, all under `src/app/**` (no teaching_admin), `tests/test_r22_workbench_*`,
   `tests/CMakeLists.txt`, `.planning/r22-workbench/**`. **Whitelist PASS.**
2. Debug-leftover scan: `git diff 911e0b17a3..HEAD -- src/ tests/ | grep -nE "^\+.*(printf|qDebug|fprintf|TODO|FIXME|XXX|HACK|unused_marker|assert\()"` → no hits. No commented-out code, no stubs. **Hygiene PASS.**
3. Green run on HEAD (env: `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/tmp/r22wb-qt:/tmp/r21a-libs:/tmp/r21a-deb`, `PROJ_LIB=…/vendor/prefix/usr/share/proj`):
   - `test_r22_workbench_ui_contract` → *All tests passed (32 assertions in 5 test cases)*
   - `test_r22_workbench_async_runner` → *All tests passed (24 assertions in 4 test cases)*
   - `test_r22_workbench_temporal_pages` → *All tests passed (30 assertions in 1 test case)*
   - `test_r22_workbench_log_tagfilter` → *All tests passed (24 assertions in 1 test case)*
   - `test_r22_workbench_view_tree` → *All tests passed (70 assertions in 2 test cases)* — run twice
     (deterministic). Without `PROJ_LIB` it fails at `REQUIRE( layer.crs().authid() == "EPSG:3857" )`
     with `PROJ: proj_create_from_database: Cannot find proj.db` — environment, not the diff.
4. **RED re-derived independently** for `test_r22_workbench_ui_contract` (the only binary that reads
   sources at runtime, so pre-fix code could be exercised without a rebuild):
   - Backed up the 8 files it reads; `git show 911e0b17a3:<file> > <file>` for each;
   - run → `test cases: 5 | 0 passed | 5 failed`, `assertions: 32 | 20 passed | 12 failed`
     (lines 53, 67, 86, 87, 88, 90, 105, 112, 115, 119, 130, 135);
   - restored all 8 from backup; `git status --short` → empty (worktree byte-identical, re-verified
     Green: 32/32). This subsumes the ledger's ui_contract RED claim and also proves the R1-fix pins
     (lines 112/115/119) are red without d8d99ee28d.
5. RED for the other four binaries: not re-run here (needs a full rebuild against reverted sources).
   Code-trace confirms each pre-fix failure mode: cursor stranded (`~AsyncRunnerBase` was empty),
   `QPointer` built from a freed bare parent (SIGSEGV), pages 6/7/8 unrouted, typed tag ignored
   (`currentData()` empty), group default action → lease leak. Ledger R4/R6 counts match my live Green
   counts exactly (24/4, 70/2, 30/1, 24/1; ui_contract 30→32 explained by the 2 assertions added in
   d8d99ee28d). No run logs were persisted for R4 — process finding R2-6.
6. Vacuity audit: no behavioral assertion passes with the fix reverted (each targets the changed
   branch/oracle; the temporal marker check requires the marker on the *current* page, not merely
   present). The ui_contract tripwires are text-based by declared design and do fail on revert (proven
   in 4) — but see R2-4 for what they cannot catch.
7. Read in full: sicnu_algorithm_dialog.{h,cpp}, gui_job_adapter.{h,cpp},
   qgsprocessingalgorithmdialogbase.cpp (closeEvent/reject/algExecuted/isFinalized/done paths),
   layer_tree_menu.cpp, active_view_host.cpp (removeLayers/zoom*), map_tool_manager.cpp,
   async_runner_base.{h,cpp}, async_gdal_runner.cpp, async_algorithm_runner.cpp, log_panel.cpp,
   temporal_analysis_dialog.cpp (kAlgorithms + all 9 pages), batch_processing_dialog.cpp (reject/onRun/
   submitNext/onBatchTaskUpdated/finishBatch), raster_processing_dialog_base.cpp, task_center.cpp
   cancel semantics (lines 470-490, 3280+), main_window_processing.cpp:120-143.

## Bottom line

Approve once R2-1 (atomic flag — one line) and R2-2 (reject() gate, or an explicit follow-up ticket
acknowledging the Esc path is out of this PR's reach) are addressed; R2-3 is a tidy-up. The W1–W9
substance, the tests, the whitelist, and the late R1-fix commit's non-race reasoning all survived
independent adversarial re-derivation.
