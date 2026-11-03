# R27-T04 Test Results

Environment: Arch/CachyOS linux x86_64, GCC 16.2.1, Qt 6.12.0, GDAL 3.13.3, ccache, build dir `build-r27-t04` (Debug, -j2 cap, flock `/tmp/exp-rs-build.lock`). All GUI tests `QT_QPA_PLATFORM=offscreen`. Host shared with sibling agent worktrees; load average 3–8 during runs.

## Build
- configure exit 0 (after cherry-picking r27-t01 `d66962c615` to repair master's broken tests/CMakeLists.txt — pristine master configure FAILS at line 17426; recorded in DECISIONS D9)
- full target build exit 0 after 5 fix cycles (missing includes QStatusBar/functional, QStringLiteral literal-only, QDialog::accept protected, rs_scan_pool TU into 17 classify-linked test targets, one transient `ld` SIGSEGV retry at -j1)
- intermittent `cc1plus`/`ld` SIGSEGV on unrelated TUs observed and worked around (existing `cmake/raise-compiler-stack.sh` + `ulimit -s 262144`; pre-existing toolchain flakiness, not caused by this branch)

## New/changed suites (this track) — single runs
| Suite | Result |
|---|---|
| test_r27_t04_roi_spectrum_reinvoke | PASS 115 assertions / 2 cases |
| test_r27_t04_classification_orphan | PASS 20 / 2 |
| test_r27_t04_recompute_async | PASS 11 / 1 |
| test_r27_t04_export_atomic | PASS 20 / 1 |
| test_r27_t04_project_downgrade | PASS 39 / 2 |
| test_r27_t04_stac_pagination | PASS 27 / 2 |
| test_r27_t04_cross_section_async | PASS 12 / 1 |
| test_r27_t04_magicwand_async | PASS 11 / 1 |
| test_r27_t04_pipeline_json | PASS 20 / 1 |
| test_r27_t04_mission_sidecar | PASS 13 / 1 |
| test_r27_t04_network_policy_gui | PASS 8 / 2 |
| test_r27_t04_async_bench | PASS (3 cases; numbers in BENCH_BASELINE.md) |
| test_roi_tool_magicwand (adapted to async landing) | PASS 173–174 / 2 |

## Double-run oracle (two consecutive full passes, all exit 0)
RUN 1 and RUN 2: 12 suites × both runs green (recorded in LOOP_LEDGER 第2轮; exact per-suite outputs in /tmp/run{1,2}-*.out at execution time).

## Regression sweep (suites whose production code this track touched) — all PASS
test_stac_client (28), test_mission_context (97), test_workspace_project_v3 (114), test_roi_spectrum_tool (18), test_spectral_profile_widget (154), test_r26_workbench_classification_ux (46), test_classification_window (33), test_interactive_session_contract (18).

## RED evidence (defect demonstrated on HEAD before GREEN)
- #1746: HEAD's recomputeSpectralCurves crashed on the orphan-classId sample (empty band vector OOB write) — reproduced during development; the landed gate + guard makes the same input pass with a visible skip message.
- #1790/#1791/#1794: HEAD performed the full GDAL work synchronously inside the slot — asserted by the async suites' first REQUIRE (no result immediately after the call), now green because the slot returns before any read.
- #1788: HEAD reset the table to page-2 only (replace) — asserted by rowCount==3 after an append landing.
- #1789: HEAD's searchNext no-op emitted zero signals — asserted by REQUIRE(unavailable.size()==1).
- #1792: HEAD's dest==src path removed the source file — asserted by source-intact checks.
- #1793: HEAD's deep-nesting parse threw out of the Qt slot (abort) — now a typed refusal (parse error string), plus stackLimit pinned.
- #1795: HEAD deleted the stale sidecar with no recoverable copy — backup-file assertions.
- #1747: HEAD overwrote the file with no SICNU block after the refusal — byte-identical assertions on the preflight path (both saveProject and saveProjectAsTo, plus the close path).
- #1745: empirical probe showed plain-QObject deleteLater+delete is absorbed by Qt (no double-free in the minimal case); the flagged window is the mixed-disposal discipline with QgsMapTool/canvas interplay — fixed to single-model release and pinned by the 100× re-invoke + teardown stress suite (documented in DECISIONS D1).
- #1765: HEAD's GUI entry never engaged the policy — behavioral (loopback /vsicurl/ refused only after engage) + source-contract assertions.

## Not executed (honesty)
- Visual/interactive smoke on a real display: NOT_EXECUTED (offscreen-only host).
- macOS notifications (#1805 F6 CGImageRelease): code-only; no macOS host — NOT_EXECUTED.
- Real STAC network pagination: covered via the client's signal seams + dialog driving (offline); live-network fetch NOT_EXECUTED (would be flaky/egress-dependent).
