# [R28-T04] Qt/QGIS workbench & cartography: atlas delivery closure, dock lifetimes, import review UX, canvas↔panel consistency, map-tool honesty

- **Base**: `50cc3255c5` (origin/master @ fetch 2026-11-04T21:03Z; re-verified before PR)
- **Branch/worktree**: `agent/r28-t04-workbench-cartography-20261104T210902Z`
- **Scope**: `src/app/**` (excl. agent_ops), `src/agent/cartography|mapspec/**`, `src/app/workflow/**`, `src/app/shell/workflow_session_controller.*`, tests `test_r28_t04_*` + own suites only. No QGIS-canvas/layer-tree rewrite, no second theme/form/registry.

## User problems solved (all verified against pristine base first)

1. **Atlas deliveries were red on master** (#1812 final status admitted it). Reproduced on a pristine baseline build: `test_cartography_production_11` 3/32 red. Two production defects + one test fixture: (a) the bounded repair loop added an auto-update legend that its own post-repair render-guard then refused — the repair was self-defeating; (b) the corpus atlas templates' declared coverage layer (`admin_regions`) was never provided by the fixture; (c) a declared-but-unresolvable coverage layer refused with misleading advice ("declare page.atlas.coverage_layer" — it WAS declared). Fixes + a new case pinning the named refusal. **cp11 now 33/33 (408 assertions) — baseline 3 red.**
2. **#1805 leftovers** (declared unfixed by #1811): **F7** ortho dialog GDALOpen on the GUI thread → bounded scan pool (generation + QPointer delivery, magic-wand discipline); **F12** template load failure wiped the layout → clone-probe first, typed refusal, working layout untouched; **F14** cartography dock leaked per-job lifetimes and bricked on lost TaskCenter terminal events → captured task ids, exactly-once delivery funnel, 5 s state-query watchdog, zombie reap on new submit; **F18** experiment-studio destructor joined the worker on the GUI thread (≤60 s freeze) → detached supervisor + qApp-context QPointer delivery.
3. **WP03**: T02's `ImportPreflight`/`ImportSession` (shipped on master) had **zero references in src/app** — batch Add-Raster/Vector only said "N succeeded, M failed". New `ImportBatchDialog` renders the session state (verdict / first actionable finding / dedup / journal honesty); refused & needs-choice sources never register; pending journal paths merge into the next batch (the resume entry). Single-file path unchanged.
4. **WP04/05 survey-verified seams**: workflow canvas `nodeSelected` was connected but **never emitted** (dead node→form seam); `runUpToNode` closed the live session before its in-flight guard; late single-job results overwrote whatever tool the user switched to; panel param edits never reached the document a Run executed (two parameter stores); stale result summaries survived parameter edits; bind-rebinding duplicated connections; Save rebranded every workflow `exported_pipeline`. All fixed; `overlayPanelParams` is static + tested.
5. **WP07**: map tools were invisible (no checked state, `toolModeChanged` dead), Identify clickable on an empty canvas (fails after the fact), Select wrongly gated on an edit session, layer removal silently discarded edit buffers, project switch carried stale map tools. Fixed A1–A4 (registry commands checkable, availability + explain for identify).
6. **#1561**: verified FIXED_IN_MASTER via `test_spectral_profile_widget` 16/16 green on the pristine baseline (evidence in baseline logs) — no re-implementation.

## NOT fixed here — handed off (T02/T03)
- **H1** (`.planning/r28-t04/HANDOFF.md`): TaskCenter/JobEngine worker threads render QGIS layouts **fully transparent** (engine sha `8a0a…` vs operator `1e3d…`, both 841×595, operator mean alpha 0.0, 99.8 % pixel AE; engine↔agent-tool surfaces byte-identical on the same thread). Every cartography node submitted through TaskCenter (dock buttons, workflow nodes) currently delivers blank "successful" exports. The byte-parity assertion for that surface is enforced under `SICNU_CP11_THREAD_PARITY=1` (same doctrine as the suite's existing `SICNU_CP11_RENDER` host gate) with an unconditional WARN naming the defect; delivery-contract assertions (valid PNG, exact page geometry) run unconditionally. An operator-side GUI-thread marshal was rejected: `TaskCenter::waitForTask` parks the main thread without event processing → deadlock.

## Tests (all local; no CI reliance)
- New: `test_r28_t04_cartography_watchdog` (F14), `test_r28_t04_experiment_teardown` (F18), `test_r28_t04_layout_template_safety` (F12), `test_r28_t04_import_batch` (WP03), `test_r28_t04_workflow_panel_consistency` (WP04/05), + F7 cases appended to `test_orthorectification_dialog` (slow-source server via /vsicurl/ for the blocking case), + 2 cases in cp11 (named refusal; corpus coverage fixture).
- Baseline (pristine build, before any edit): whole T04 family green except cp11 3 red — logs in `.planning/r28-t04/baseline-logs/`.
- **Final: 15/15 suites ALL PASS, run twice consecutively, exit 0 both passes** (cp11 33/33 cases · 408 assertions; new suites: watchdog 3/11, teardown 1/38, template-safety 1/27, import-batch 4/23, workflow-consistency 6/294; ortho dialog +3 F7 cases → 4/40; full family regression green). New failures vs baseline: zero. Details: `.planning/r28-t04/TEST_RESULTS.md`.
- Commands: `cmake --build build-r28-t04 --parallel 2 --target <suites>` under `scripts/dev/resource_guard.py` dual locks; runs via `QT_QPA_PLATFORM=offscreen SICNU_CP11_RENDER=1 ./build-r28-t04/tests/<suite>`.

## Shared-file discipline
- `tests/CMakeLists.txt`: append-only `# ── R28-T04` block (5 suites), no existing target touched.
- `src/app/CMakeLists.txt`: one new source line. `.gitignore`: per-track whitelist per convention.
- PR #1813 overlap note: it edits `cartography_dock.cpp`/`experiment_studio_dock.cpp` layout rows only; my hunks are logic — re-diffed before merge; `sicnu_zh_CN.ts` left to #1813's lane (my new strings ride the next lupdate pass).

## Reviews
- **R1** (independent agent, fresh context; report: `.planning/r28-t04/REVIEW_R1.md`): 1 P1 + 4 P2 + 8 P3. The P1 (session-capture cleared-before-read — the late-result capture was dead code) plus all P2 and 5 P3 FIXED (commit 326c3d1668 and follow-ups); 3 P3 recorded with rationale. R1 also verified-sound: atlas coverage plumbing, F18 teardown thread-safety, F7 hop, F12 probe, watchdog exactly-once, API additivity, WP07 shape.
- **R2**: FILL.
- Merge/registration integrity: `check_merge_registration_integrity.py --base 50cc3255c5 --head HEAD` → 0 lost, 0 duplicates, +5 targets; fresh configure green. master had not moved from the base SHA at PR time (verified by fetch before push).
- **Open-PR collision notes**: #1813 (UI polish; layout-only hunks in the two dock files I touched logically, plus `sicnu_zh_CN.ts` — left to its lane); #1814 (T03) edits `src/app/main_window.cpp` + `tests/CMakeLists.txt` (append-only blocks: keep both, per repo convention); #1815 (T01) `tests/CMakeLists.txt` only.

## NOT_EXECUTED
- Real-X11 visual pass of the new dialog/toolbars (host has :0 + ImageMagick — deferred to merge-time smoke if budget allows; offscreen suites cover logic).
- Windows/macOS lanes; SICNU_CP11_THREAD_PARITY strict mode (host lacks worker-thread QGIS render parity — that's H1).
