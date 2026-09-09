# Issue map — defect → milestone → fix → test

| Issue | Milestone | Fix | Pinning test |
|-------|-----------|-----|--------------|
| #777 InspectorHost::rebuildTabs `delete oldTabs` UAF | A | Reparent InspectorSection pages out of the dying QTabWidget before deletion; never destroy registered sections | test_inspector_host: supported→unsupported→supported cycle asserts section pointer stability + no crash |
| #778 SelectionContext dangling canvas/layers | A | QPointer for canvas/tree/host; purge-on-`layerWillBeRemoved`; liveness recheck (QPointer mirror) invalidates cache; refreshNow on removal | test_selection_context: remove layer → snapshot has no dead pointers; destroy canvas → snapshot safe |
| #779 layer destroyed during active render | A/B | `QgisDisplayManager` settles rendering (stop + blocking job settle via vendored-canvas `stopRenderingAndSettle`) before store removal — removeLayer, relocateLayer, removeView | test_layer_sync_contract: removeLayer during `renderStarting` handler; no crash, layer gone, canvas idle |
| #780 test masks re-selection UAF | A | new re-selection cycle case in test_inspector_host (#777 test) | same |
| #792 shortcut-owner assert abort | D | `QSet<QString> m_shortcutOwners`; one installation per command id; warn + no-op on repeat, never abort | test_command_registry: two distinct commands installShortcut=true; same-command repeat warns not aborts |
| #793 refreshCanvasLayers reads global tree | B | read active view's own QgsLayerTree via new `QgisDisplayManager::viewLayerTree()`; fall back to project root only when view unknown | test_active_view_host_data_context: secondary view with unchecked-in-main layer set → secondary canvas shows view-local set |
| #794 no installShortcut test | D | test_command_registry installShortcut cases (#792) | same |
| #795 conflict test single-file | D | expand test_shortcut_conflicts to scan command_defs.cpp, main_window_workbench.cpp, ribbon_controller.cpp + registry definitions() at runtime | test_shortcut_conflicts new CASES |
| #796 no async render race test | B | test_layer_sync_contract: removeLayer inside `renderStarting`; wait for idle | same |
| #797 unbounded GDAL on global pool | C | dedicated bounded `rs_scan_pool` (max 2 threads) + cooperative generation-based cancellation for ROI/histogram | new test_scan_pool (+ widget epoch behavior) |
| #798 sync sub-job deadlock | C | JobEngine: worker-originated submit raises transient capacity so a blocked worker's sub-job is always runnable; documented refusal API for blocking waits on worker threads | test_job_engine: saturate pool(2) with 2 bodies that submit+wait sub-jobs → both complete |
| #799 TaskCenter submit/registration race | C | pre-register mapping with a caller-chosen job id (`JobEngine::submitWithId`) BEFORE submit; rollback on failure | test_task_center: executor completing before flush registration → task reaches terminal state |
| #800 DataManager const affinity | C | `Q_ASSERT`/warn affinity helper in every const accessor | test_data_manager_reap: off-thread reader triggers assertion path in debug (contract captured via test hook) |
| #812 inspector currentChanged unwired | G | connect currentChanged → populate newly shown section (lazy-on-first-show honored) | test_inspector_host: switch tab → section populates |
| #813 external adapters lack lifecycle hooks | F | shell installs windowGetter/dirtyFn/inFlight/cancel/close hooks per external bench (classify, georef×2, obia) using existing window accessors | test_workbench_host / test_interactive_session_contract: classify bench reports dirty/in-flight; close confirms |

Non-issue work (features, not defect fixes): Milestones E (ContextRules
reasons), G (SAR/vector inspector sections), H (schema form hardening),
I (task/result states), J (IA/tokens), K (a11y/keyboard).
