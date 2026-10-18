# R16-D-T07 Goal Loop Ledger (Qt/QGIS workbench)

Oracle protocol: RED (real failing run on master, captured) → fix → GREEN×2 (two consecutive identical direct-binary runs).

## Phase 0/1
- [x] master @ 46791bc65, isolated branch/worktree; #1517 file-overlap verified disjoint (ribbon_controller.cpp NOT in #1517 diff)
- [x] Audit findings T07-F01..F11 (independent read-only auditor, integrator spot-checked F01/F03/F05/F06/F07 verbatim) → ../r16-d/WORK_PACKAGES.md
- [x] Baseline families GREEN×1 on clean master build (11/11 via ctest, 2026-10-17)

## RED evidence (captured on master: production stashed, tests-only builds)
| WP | RED run | Evidence |
|----|---------|----------|
| WP-1 lease lifecycle | test_r14_D_vector_edit_honesty "*Edit lease is held*" on master | `hasActiveEditLease` FALSE (lease not held); second acquire SUCCEEDED (exclusivity inert); then **SIGSEGV** in saveEdits (assetChanged→relocateLayer destroyed the editing layer mid-call — the P0 live) |
| WP-1 relocation | test_qgis_display_manager "*Relocation defers*" on master | `CHECK_FALSE(watched.isNull())` failed → editing layer destroyed with buffered edits; subsequent raw-pointer use **SIGSEGV** |
| WP-2/WP-3 gating+reasons | honesty suite "*Digitizing tools*" on master | geometry checks + reason-property checks failed (actions enabled regardless of geometry; no reason channel) |
| WP-5 setLayerVisible | display manager suite on master | `REQUIRE_FALSE(result)` failed → typed success returned while nothing applied |
| WP-7 piecewise | guard (not RED) | bisection is a value-equivalent refactor; the test is a regression guard (6109-assert equivalence sweep; passes on master BY DESIGN) |
| WP-4 ribbon / WP-6 context tool / WP-8 georef | family suites + code | honest oracle limits documented in IMPLEMENTATION.md (no standalone ribbon/georef widget test targets; context-tool lambda unreachable from unit targets) |

## GREEN×2 (final, after probe removal; identical both rounds)
- test_r14_D_vector_edit_honesty: 144 assertions / 10 cases ×2 (exit 0)
- test_qgis_display_manager: 6489 / 25 ×2 (exit 0)
- test_display_stretch: 41 / 12 ×2 (exit 0)
- test_magic_wand_roi: 24 / 3 ×2 (exit 0)
- test_workbench_host: 50 / 8 ×2 (exit 0)

## WP status
| WP | RED | Fix | GREEN×2 |
|----|-----|-----|---------|
| WP-T07-1 lease lifecycle + relocation guard | RED×2 incl. SIGSEGV×2 | m_editLeases hold; all commit/rollback paths routed; layerWillBeRemoved hook; relocateLayer defers editable layers (typed display.relocate_deferred_editing; retried via retryPendingRelocations at every lease release — R1; success-only marker clear — R2); saveEdits/toggleEditing/confirmSaveEdits (save AND discard paths — R2) re-resolve by layer id + displayLayerId | ✅ |
| WP-T07-2 disabled-without-reason | RED | reason channel (tooltip suffix + sicnu/unavailableReason, restorable base); ribbon keeps accessibleDescription in both states | ✅ |
| WP-T07-3 geometry gating | RED | per-action masks (rings polygon-only; reverse line-only; offset line+polygon [polygon mode]; split/reshape/simplify line+polygon; delete-part geometry-agnostic — corrected in review R1) | ✅ |
| WP-T07-4 ribbon fallback raster | family | currentRasterLayer() drops first-raster fallback | ✅ |
| WP-T07-5 setLayerVisible fake success | RED | typed display.layer_tree_node_missing | ✅ |
| WP-T07-6 workbench:context mutation | family | projection local; no m_mission write | ✅ |
| WP-T07-7 StdDev caps + bisection | guard | 250k caps ×3; lower_bound lookup | ✅ |
| WP-T07-8 georef theme/RMSE | code-verified | palette base brush; "RMSE: — (no fit)" | ✅ |

Deferred: T07-F08 magic-wand unbounded read (latent, no production canvas hook).
