# R35-T06 — CROSS-TRACK HANDOFF

Each item: exact path, actual vs expected contract, owner, minimal input,
evidence. Items inherited from the R33 ledger are marked *(inherited)* — they
were not re-investigated this session and their evidence still stands.

## H-01 *(inherited)* → T04: i18n backend text
`src/science_context/capability_router.cpp`, `src/teaching/lab_feedback_projection.cpp`,
`src/teaching/lab_session_state.cpp` used to break the canonical gate. **That gate
is green at `120677e6`** (`SELFTEST PASS`, `GATE PASS`), so this is closed as a
gate item; the files remain T04's if their `nonui_zh` counts drift again.

## H-02 → T06 later: `sicnu_zh_CN.ts` context hygiene (issue #2082)
The TS now carries four documented **extraction-alias** contexts for template
classes (`T`, `QgsAbstractContentCacheBase`, `QgsAbstractContentCache`,
`QgsProjectStoredObjectManagerModelBase`, `QgsProjectStoredObjectManager`). This
is the repository's existing convention, but it is not free: each alias is a
place where the runtime context must be re-proven. Issue #2082 ("5 zh_CN contexts
keyed by short class name while moc registers the qualified name") is the same
class of bug and needs a per-string C++-side proof like the one done for
`Field Mapping` (nested class → enclosing QObject `tr`, `qgsfieldmappingmodel.cpp:653`).

## H-03 *(inherited)* → T06 next session: cartography draft vs compiled layout
`src/app/cartography/cartography_dock.cpp`: `adoptSpec()` (537-545) writes
`m_composedLayoutName` unconditionally; `runCompose()` (566-569) calls it
**before** reading `compiled`. Export/Designer gates (496, 511-521) and
`runExport()` (648, 662) key off that single string, so a compose that reported
`compiled=false` still enables "compiled layout" actions.
**Cost to verify:** `tests/test_r25_d_cartography_dock.cpp` links
`sicnu_geo_rs_shell` (the whole application) — a separate multi-hour build on
this host. That is why it was not attempted here (DECISIONS D-06).

## H-04 *(inherited)* → T06: multi-page/atlas failure vs the previous delivery
`src/agent/cartography/export.cpp` removes only this run's `delivered` pages
(286-287, 335-336, 395-396). Whether a previous generation can be collaterally
removed still needs the A→B two-page fixture. Reuse T03's `atomic_fs` APIs; do
not add a second atomic writer or touch the public committer (T04).

## H-05 *(inherited)* → T06: history truncation label
`src/app/workbench/processing_history_model.cpp:30-42` — `m_droppedCount +=` is
cumulative **by contract** (`test_r30_t06_history_action_parity` pins the second
`setEntries` giving 4000). Only the label may need a separate current-snapshot
field; never zero the accumulator.

## H-06 *(inherited)* → T06: Studio report identity / curve truth
`src/app/experiment_studio/experiment_studio_dock.cpp`: `curves.first()` at 2113;
`.toDouble()` at 2121-2122 turns a missing `mean` into a real 0. The parser's
validity contract lives in `src/study/study_export.cpp:265-286` (**T04**) — the
UI must not infer "missing" from 0.

## H-07 → T05: nothing owed
No Agent/Mission file was touched. `src/app/main_window_workbench.cpp` remains
T06's; T05 should route any wiring request through this track.

## H-08 → T06 later: preview file watching
The detail pane still refreshes only on selection. `identityChanged` triggers one
bounded re-read (R33), not a subscription. A `QFileSystemWatcher` on the current
preview source with debouncing would close the gap; it needs
`test_data_manager_panel`, i.e. the application target.

## H-09 *(new)* → whoever builds next on this host: the environment is degraded
Three independent, reproducible blockers were hit and are documented in
ACCEPTANCE.md "Environment":
1. `git worktree add` of this repo (16 385 files) takes 40+ min and is killed by
   the tool timeout at ~9 files/s under the current load.
2. `utimensat(2)` returns `EPERM` for **every** path including `/tmp` and
   `$HOME`, so `touch` fails to set times (a plain minimal CMake project still
   configures and builds fine, so this is not a compiler problem).
3. Background tasks run in **separate PID namespaces**, so a `resource_guard`
   lock written by one task can never be validated — and never reclaimed — by
   the next one. A dead lock must be removed explicitly (`python3 -c
   "os.unlink(...)"`; the `rm` shim fails silently on bulk deletes).
