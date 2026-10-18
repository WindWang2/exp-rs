# R16-D Integration Test Results (final)

All runs: `QT_QPA_PLATFORM=offscreen`, direct binary execution from the integration build dir (`build-r16-d`, Ninja Debug, -j4 under the shared flock lease), `LD_LIBRARY_PATH` per the #730 test-harness pin. Exit codes recorded; identical assertion counts across both rounds.

## Final integration runs (after ALL review fixes, HEAD = final commit)
| Executable | Round 1 | Round 2 |
|---|---|---|
| test_mapspec | exit 0 — 2807 assertions / 236 cases | exit 0 — identical |
| test_cartography_production_11 | exit 0 — 222 assertions (21 pass + 5 skip: documented host legend-render gate) | exit 0 — identical |
| test_r14_D_vector_edit_honesty | exit 0 — 152 assertions / 11 cases | exit 0 — identical |
| test_qgis_display_manager | exit 0 — 6490 assertions / 25 cases | exit 0 — identical |
| test_display_stretch | exit 0 — 41 / 12 | exit 0 — identical |
| test_magic_wand_roi | exit 0 — 24 / 3 | exit 0 — identical |
| test_workbench_host | exit 0 — 50 / 8 | exit 0 — identical |

(The first integration double-run, recorded in the session log before the R2 fix commit, covered the same families GREEN×2 at the R1-fix state; the table above is the final post-R2 rerun.)

## Track-level runs
- T08 track (build-r16-d in exp-rs-r16-d-t08): test_mapspec 236/236 (2805→2807 assertions as oracles landed) ×2; production_11 21+5skip ×2.
- T07 track (build-r16-d in exp-rs-r16-d-t07): RED phase on stashed master (2 SIGSEGV live demonstrations + assertion failures, see track ledger); final families ×2 GREEN.

## Known environment states (not this PR)
- production_11 host-render gate (QgsLayoutItemLegend::paint hang; pre-existing differential evidence in .planning/cartography-production-11/EVIDENCE.md): 5 cases SKIP by default; SICNU_CP11_RENDER=1 reproduces the hang-class failures identically on untouched master.
- ctest-level strays in minimal build dirs: pi_* script tests (environment), test_i18n_string_audit (pre-existing failure seen on clean master), classroom_batch_conformance (flaky runner exit — Failed then Passed on re-run, pre-existing). ~960 `_NOT_BUILT` placeholders are test targets outside this PR's build scope.
- A previously recorded "1247 passed / 2 failed out of 1252" ctest baseline number was RETRACTED (inferred from a 6-line tail; see T08 TEST_MATRIX.md).
