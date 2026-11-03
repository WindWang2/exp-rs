# R27-T04 Oracles (each AC = a failing-command → passing-command pair)

All test files use prefix `tests/test_r27_t04_*`. Each oracle runs twice consecutively before PR.

| # | AC | Oracle command (exit 0 = pass) | RED evidence |
|---|---|---|---|
| O1 | ROI spectrum tool survives 100 re-invokes with half-drawn polygon + deferred-delete processing + main-window destruction: no crash/double-free | `ctest -R test_r27_t04_roi_spectrum_reinvoke` | New test fails on HEAD (double-free) or is proven to crash under sanitizer; must fail via detection of the defect mechanism |
| O2 | Classification spectral recompute with orphan classId (from sample layer) does not write OOB; unknown ids skipped with visible status; merge rollback keeps classDefs | `ctest -R test_r27_t04_classification_orphan` | OOB write detected (UBSan/guard) or orphan handling assertion fails on HEAD |
| O3 | Downgrade refusal leaves on-disk SICNU extension block byte-identical; save aborted (no overwrite); "Save As" to new path works | `ctest -R test_r27_t04_project_downgrade` | HEAD writes a project without extension block (block bytes lost) |
| O4 | STAC More appends (page 2 keeps page 1 rows); no-op searchNext terminates UI state; error search hides More | `ctest -R test_r27_t04_stac_pagination` | HEAD replaces rows / button stranded |
| O5 | Cross-section + magic wand + classification recompute perform no sync GDAL open/read on GUI thread: worker used, cancel/supersede works, event loop stays responsive (p95 slot time recorded, threshold = baseline measurement) | `ctest -R "test_r27_t04_(cross_section|magicwand|recompute)_async"` | HEAD performs sync IO in slot (asserted by instrumentation seam) |
| O6 | Export dest==src refused (source intact); failed publish leaves prior dest intact (atomic) | `ctest -R test_r27_t04_export_atomic` | HEAD deletes source |
| O7 | Deep-nested pipeline JSON yields user error, not abort | `ctest -R test_r27_t04_pipeline_json` | HEAD aborts (uncaught exception) — RED shown by exception-catching harness or sanitizer run |
| O8 | Mission dual-channel failure keeps original recoverable (backup file exists, restore ignores it, no data loss) | `ctest -R test_r27_t04_mission_sidecar` | HEAD deletes original |
| O9 | GUI process network policy engaged → raw /vsicurl/ private-address open refused | `ctest -R test_r27_t04_network_policy_gui` | HEAD allows the open (policy not engaged at GUI entry) |
| O10 | All targeted tests ×2 consecutive runs green; no whitelist violations in `git diff --name-only $(git merge-base origin/master HEAD)..HEAD` | compound shell check | n/a |

Notes:
- "p95 slot time" recorded in BENCH_BASELINE.md with command + numbers; no "faster" claims without numbers.
- Visual smoke without display device → marked NOT_EXECUTED in TEST_RESULTS.md.
