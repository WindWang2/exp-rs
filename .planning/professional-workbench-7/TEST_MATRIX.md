# TEST MATRIX — scenario × vehicle (updated per milestone)

Legend: ⬜ planned · ✅ passing locally (Release, offscreen, CTEST_PARALLEL_LEVEL=1)

| # | Scenario (goal §测试) | Vehicle | Status |
|---|----------------------|---------|--------|
| 1 | quit with dirty + in-flight bench → confirm, cancel routed | test_workbench_shutdown_policy | ⬜ |
| 2 | project close while task running | test_workbench_shutdown_policy + task-center case isolation | ⬜ |
| 3 | dirty external window blocks close; cancel path | test_workbench_shutdown_policy (fake external bench) | ⬜ |
| 4 | deleted layer in provenance inspector | test_provenance_section | ⬜ |
| 5 | layer with derivation chain → records rendered; unknown → truthful empty | test_provenance_section | ⬜ |
| 6 | history shows running/completed/failed/cancelled/interrupted | test_processing_history_model | ⬜ |
| 7 | 100k-row history model stays bounded (virtual fetch, no widget/row) | test_processing_history_model scale case | ⬜ |
| 8 | rerun routes through TaskCenter re-submission (no second executor) | test_processing_history_model (action seam) | ⬜ |
| 9 | temporal paged browser over large collection (synthetic 100k dates) | test_temporal_workbench_panel | ⬜ |
| 10 | workbench switch keeps panels coherent (no stale selection) | test_selection_context (regression) + new panel tests | ⬜ |
| 11 | dataset samples 100k paged/lazy; leakage/stats cancel | test_dataset_workbench_panel | ⬜ |
| 12 | model readiness failure → verbatim error, no fake success | test_model_workbench_panel | ⬜ |
| 13 | keyboard-only critical flow (palette → history → provenance) | registry shortcut tests + section keyboard nav test | ⬜ |
| 14 | repeated open/close lifetime (panels, benches) | lifetime loop cases in new tests | ⬜ |
| 15 | multi-view isolation regression | test_layer_sync_contract / display manager (inherited) | ⬜ |
