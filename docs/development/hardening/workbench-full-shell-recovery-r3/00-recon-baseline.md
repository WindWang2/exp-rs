# Recon baseline — Workbench / Full-Shell Fixture / Project Lifecycle Recovery R3

Live at execution start (2026-09-25):

- `origin/master` = `618a9afa31bbeb4b17f11f4feb4da723d30dd745`
  (`fix(ci): jsoncpp for D15 Package G studio workbench tests (#1309)`) — one
  commit past the 2026-09-24 recon seed (`3487b9ad8`).
- Open PRs: **0**. Open issues: **0**. No parallel-PR collision; no
  semantic-union dedup needed at branch point.
- Sibling remote branches present but untouched:
  `hardening/r3-teaching-cockpit-operator-grading-r3` /
  `hardening/r3-teaching-admin-authority-convergence-r3` (both at `c26389d06`,
  teaching tracks — different module scope, absorbed by their own PRs).
- Worktree: `/home/kevin/project/exp-rs-r3-workbench-full-shell-recovery-r3`,
  branch `hardening/r3-workbench-full-shell-recovery-r3`, HEAD ==
  merge-base == origin/master.

## What prior tracks already fixed (not repeated here)

- #1284 (`completion/workbench-project-lifecycle-shell-fixtures`):
  transactional project open (`workbench/project_session_boundary.*` with
  ReadFailed rollback of phantom fileName + governance store), secondary-view
  session object (`shell/secondary_map_view_session.*`, sync controller
  rebuilt per open), layout-designer retirement (QObject `destroyed`-based),
  scoped SpatialTool registration tokens.
- #1097: `saveProjectAs` failure keeps prior identity; `QgsProject::write`
  itself is crash-safe (temp file + atomic rename, `qgsproject.cpp:3621+`),
  so a failed Save As cannot truncate an existing target.
- #1269 / #1083 / #1052 / #1084: story boundary, probe-first open, honest
  save failure reporting, commit-fail-closed.

## Prior track's own recorded residuals (= this track's targets)

From `workbench-project-lifecycle-shell-fixtures/01-status-matrix.md` §5:

1. **Full-shell offscreen fixture** — "全窗口 fixture 仍为后续方向"; the four
   delivered fixtures are component-level. Combined
   project+docks+editing+mission+teaching+experiment state is still only
   exercised by construction, not by any test.
2. **Failed-open side effects** — the boundary unbinds the phantom identity,
   but the WINDOW-level failed-open contract (title, lab recording context,
   mission watcher, governance panel, status surfaces) has no test and no
   explicit contract statement.
3. B12 (`restoreState` return value unchecked), B13 (`layerWasAdded`
   duplicate connection), B14 (saved toast contradiction) — recorded in
   #1269, unowned since.

## Gaps proven on current master (file:line evidence)

- **G1 — lab recording context survives a failed open.**
  `main_window_project.cpp:302-315`: the open transaction's
  `onSessionEmptied` hook calls `stopLabRecording()` (recorder disabled) but
  never `setLabRecordingContext(QString(),…)`. On `ReadFailed` the window
  then renders the empty session (`:325-345`) while
  `m_labExperimentDbPath/m_labExperimentId/m_labWorkspaceRoot` still name the
  previous project's lab db — `newProject()` clears them
  (`main_window_project.cpp:246-247`); the open-failure path does not. Any
  consumer of the recording context (lab cockpit dock header) keeps
  presenting a recording context for a project that is no longer open.
- **G2 — window-level lifecycle has no test at all.** The only test
  instantiating shell code is component-level
  (`test_project_session_boundary` uses headless ProjectContext;
  `test_shortcut_hosting` uses a bare QMainWindow). Dialog-bound entry
  points (`QgisDesktopWindow::openProject/saveProjectAs` call
  `QFileDialog::get*FileName` directly, `main_window_project.cpp:282,398`)
  make the real shell paths untestable without a seam.
- **G3 — SaveAs sidecar/watcher rebind + Unicode targets have no
  window-level oracle.** `saveProjectAs` (`main_window_project.cpp:396-429`)
  rebinds store + mission watcher on success and rolls identity back on
  failure, but nothing pins: watcher actually armed on the NEW sidecar,
  governance store actually reopened at the target, Unicode directory
  round-trip, or failed-SaveAs leaving an existing target byte-identical.
- **G4 — new/open story-boundary context reset is only pinned at the
  boundary level.** `resetMissionSessionState` +
  `stopLabRecording` are wired (`main_window_project.cpp:304-315,246-250`),
  but no test drives A→new→B (or A→fail→B) through the window and asserts
  `missionContext()`, lab context, tool tokens and the mission panel are
  clean.

## Constraints discovered

- No `ccache` on this host; Ninja + Debug + `USE_PRECOMPILED_HEADERS=ON`.
  A fresh build tree must compile the bundled QGIS (`qgis_core` ≈1054 TUs)
  exactly once; afterwards all verification is narrow (`--target`).
- `SICNU_EMBED_PYTHON=OFF` and is a `sicnu_geo_rs`-private define — the
  `Sicnu::qgis_display` static library (which contains `main_window.cpp`)
  links into a test without Python.
- App resources (`resources/icons.qrc`, theme QSS) are compiled into the
  `sicnu_geo_rs` executable, not the library — a test-hosted window must
  tolerate missing resources (`applyUiTheme` already degrades to a warning,
  `main_window_misc.cpp:81-99`).
- `QgisDesktopWindow` construction requires `QApplication`; offscreen
  platform precedent: `test_secondary_map_view_session` (real canvases,
  plain `QApplication`, no `initQgis()`).
- QSettings state restore (`restorePanelState`, theme) must be isolated:
  test processes set unique organization/application names and clear the
  location before constructing the window.
