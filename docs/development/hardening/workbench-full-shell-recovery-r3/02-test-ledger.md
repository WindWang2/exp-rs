# Test ledger — Workbench / Full-Shell Fixture / Project Lifecycle Recovery R3

Environment: `build-r3w` (Ninja, Debug, PCH ON, no ccache), Qt 6.11.2, bundled
QGIS built from source (`qgis_core`/`qgis_gui`/`qgis_analysis` — one-time
bootstrap; every later check was a named `--target` build). Test runtime env:
`LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib QT_QPA_PLATFORM=offscreen`.

Verification order per contract D: changed-TU objects → leaf libs → named
unit targets → the two new E2E targets. `test_build_wiring_drift` re-run for
the CMake change.

## GREEN evidence (all on the final tree, no sabotage markers)

| Target | Scope | Result |
|---|---|---|
| `test_workbench_full_shell_lifecycle` | offscreen full `QgisDesktopWindow`; open/open-fail/save/new/SaveAs(dir-refusal + doomed-write)/close/secondary-view/designer/shutdown/restore-state | **ALL PASSED 116/116 assertions, 7/7 cases — run twice consecutively** (126/126 after review round) |
| `test_project_context_run_mirror` | headless; run-mirror connect-once over a real tracked pipeline | **ALL PASSED 12/12 assertions — run twice consecutively** |
| `test_project_session_boundary` | pre-existing boundary suite (adjacent surface) | ALL PASSED 41/41 assertions, 5/5 cases |
| `test_secondary_map_view_session` | pre-existing secondary view session (adjacent) | ALL PASSED 56/56 assertions, 6/6 cases |
| `test_layout_designer_lifecycle` | pre-existing designer retirement (adjacent) | ALL PASSED 18/18 assertions, 4/4 cases |
| `test_spatial_tool_registration` | pre-existing tool token suite (adjacent) | All tests passed (34 assertions, 5 cases) |
| `test_build_wiring_drift` | CMake wiring drift oracle (tests/CMakeLists.txt + src/app/CMakeLists.txt changed) | All tests passed (34 assertions, 7 cases) |
| `sicnu_geo_rs` | product executable relink after shell TU membership moved to `sicnu_geo_rs_shell` | linked clean |

## Sabotage (RED) evidence — every oracle kills its regression

| Sabotage | Result |
|---|---|
| S1 remove `setLabRecordingContext(QString(),…)` from `resetSessionStoryState` | `[story_boundary]` RED 19/22 — lab db/id/root asserts fail (test .cpp:308-310) |
| S2 remove `setMapTool(m_panTool)` from `renderEmptySessionShell` | `[story_boundary]` RED 21/22 — pan-tool assert fails (test .cpp:312) |
| S3 remove projectRef re-home from `saveProjectAsTo` | `[save_as]` RED 21/23 — projectRef asserts fail on both paths (test .cpp:346,363) |
| S4 restore per-open mirror connect in `openWorkspaceStore` | `run_mirror` RED 11/12 — deliveries ≠ emissions (test .cpp:208) |
| S5 ignore `restoreState` return (master B12 shape) | `[b12]` RED 4/5 — corrupt blob survives construction (test .cpp:497) |

All sabotages reverted; final tree grep-verified free of `SABOTAGE` markers
before the double GREEN run above.

## Review round 1 (independent adversarial review) and fixes

Verdict on `65c6e73bb`: NOT-READY, blocking P1-1 — all blocking findings fixed
on the final tree, suites re-run green.

| Finding | Disposition |
|---|---|
| **P1-1** failed Save As leaves `m_missionRuntime.context.projectRef` on the failed target (onProjectWrite mirrors the re-homed context during the doomed write), silently disabling mission reconciliation | **Fixed**: the rollback now restores `m_missionRuntime.context.projectRef` together with the other refs; new doomed-write scenario (read-only parent, writeProject emitted before the failure) asserts context ref restoration on the live window |
| **P2-1** failed Save As publishes an orphan mission sidecar beside the failed target (later adoptable into an unrelated project) | **Fixed two-sided**: directory targets are refused before the transaction mutates anything; a sidecar that did not exist before the transaction is removed by the rollback (pre-existing sidecars left untouched); absence of the orphan is asserted in the doomed-write scenario |
| **P2-2** rebasing branches could append `target_sources(sicnu_geo_rs ...)` cleanly and land TUs on the exe, silently bypassing the full-shell test | **Fixed**: configure-time split-drift guard in `src/app/CMakeLists.txt` — `get_target_property(SOURCES sicnu_geo_rs)` must be `main.cpp` + `.qrc` only, else `FATAL_ERROR` with a pointer to the shell library |
| **P3-1** `>=` gate lets newer-shell state be destroyed by an alternating older binary | **Fixed (softened)**: `savedVersion > k` now restores nothing and rewrites nothing — newer state passes through untouched; test flipped to pin preservation |
| P3-2 duplicated exe/shell link lists; exe-side `Sicnu::teaching` residue | **Kept deliberately** (belt-and-suspenders for parallel-branch rebases: a dep added exe-side still links the exe); recorded here |
| P3-3 mirror now runs with no store open | Verified safe: `GovernanceStore` refuses `!m_impl`; residual unconditional `entityChanged` broadcast on failed upsert noted upstream |
| P3-4 modal dismisser / QSettings isolation / pumpUntil | Reviewed sound (budget-bounded, per-process namespace, predicate-driven settle) |
| P3-5 stale `v9/v12` comment | Fixed (comment now lists actual versions) |

## Final GREEN (post-review tree)

| Target | Result |
|---|---|
| `test_workbench_full_shell_lifecycle` | **ALL PASSED 126/126 assertions, 7/7 cases — twice consecutively** |
| `test_project_context_run_mirror` | ALL PASSED 12/12 |
| `test_build_wiring_drift` | All tests passed (34 assertions, 7 cases) |
| `[save_as]` focused (dir-refusal + doomed-write rollback) | ALL PASSED 33/33 |
| `[b12]` focused (corrupt drop + newer-state preservation) | ALL PASSED 5/5 |

## Notes

- First full-suite run wedged in `checkUnsavedChanges`'s modal prompt: a
  fresh project open can already carry the modified marker (state re-stamped
  during the read), so `newProject()` legitimately prompts. The fixture arms
  a scheduled Disard answer for the boundary stories — dialog plumbing, not
  race masking (the answer fires inside the box's own nested exec loop).
- The initial `vector::pop_back` assert in the first wedged run was a
  Catch2 `ReusableStringStream` secondary abort while reporting the
  timeout SIGTERM, not a shell defect; backtraced via coredumpctl to
  `checkUnsavedChanges → QDialog::exec` as the wedge point.
- `main_window.h` is reported `unwired` by narrow_targets (headers have no
  target); its consumers (main_window_*.cpp → `sicnu_geo_rs_shell`,
  `sicnu_geo_rs`) are the mapped targets and were built.
- Full-window fixture tolerates the missing exe-side RCC resources
  (`icons.qrc`, theme QSS stay in `sicnu_geo_rs` per the linker constraint)
  — construction degrades to warnings, no test depends on themed resources.
