# Test matrix — scenario × evidence

Status legend: ✅ passing locally (Release, offscreen, sequential runs)
Host: Windows 11, MSVC 14.38, -j2 build, CTEST_PARALLEL_LEVEL=1

| # | Scenario (goal §5) | Test vehicle | Status |
|---|--------------------|--------------|--------|
| 1 | open → select → inspect → run operator → task/result → open artifact | test_active_view_host_data_context ✅, test_workbench_host ✅, task/result UX unchanged from 5.0 (RsJobPanel) | ✅ |
| 2 | add/remove layer while canvas rendering — no crash | test_layer_sync_contract "removal during active render" ✅; test_qgis_display_manager "Removing a Display Layer during an active canvas render settles first (#779)" ✅ | ✅ |
| 3 | two map views without layer-state leakage | test_active_view_host_data_context "refreshCanvasLayers reads the ACTIVE view's tree (#793)" ✅ | ✅ |
| 4 | unsupported → placeholder → re-select supported without UAF | test_inspector_host "sections survive the re-selection cycle" ✅ | ✅ |
| 5 | toggle vector editing → availability updates coherently | test_selection_context editing rules ✅ + registry refresh | ✅ |
| 6 | disabled palette command visible with deterministic reason | test_command_registry reasons ✅ + ContextRules rs.* reason coverage ✅ | ✅ |
| 7 | long ROI/histogram op: bounded pool, cancel works | test_scan_pool (bounds, generations, targeted cancel, checkpoint exit) ✅ | ✅ |
| 8 | workbench dirty/in-flight influences close | test_workbench_host lifecycle-hook cases ✅, test_interactive_session_contract ✅ | ✅ |
| 9 | keyboard-only map/command workflow | test_shortcut_conflicts multi-source scan ✅ + test_command_registry installShortcut cases ✅ | ✅ |
| 10 | 10k/100k workspace model/view bounded | existing workspace bounds suites (not in this track's scope; kept green — no regression: workspace suites untouched) | ✅ (inherited) |

## Defect-pinning tests (all green)

| Defect | Test target | Result |
|--------|-------------|--------|
| #777/#780 | test_inspector_host re-selection cycle | ✅ |
| #778 | test_selection_context removal purge + canvas destruction | ✅ |
| #779/#796 | test_layer_sync_contract + test_qgis_display_manager mid-render removal | ✅ |
| #792/#794 | test_command_registry installShortcut | ✅ |
| #795 | test_shortcut_conflicts multi-file scan | ✅ |
| #798 | test_job_engine saturated-pool blocking sub-jobs | ✅ (34/34 cases) |
| #799 | test_job_engine submitWithId refuse/free + test_task_center instant-job mapping | ✅ |
| #800 | test_data_manager_reap off-affinity warning | ✅ |
| #812 | test_inspector_host tab-switch populate | ✅ |
| #813 | test_workbench_host lifecycle hooks + safe defaults | ✅ |

## test_task_center on this host (documented, pre-existing)

All **31/31 cases pass in isolation** (each in its own process) on this
branch. Run as ONE process, the admission/RSS-gate cases (lines ~800, 990,
1079, 1171, 1383) are timing-flaky on this Windows host and the suite then
hangs on a later admission case. **A/B proof this is pre-existing and not
caused by this track**: the same full-sequence run with this track's
TaskCenter change stashed (baseline) fails at the identical lines with the
identical signature and hangs identically. Individual-case evidence covers
the track's own #799 mapping case.

## Local run recipe

```
run_ux6_tests.cmd                      (worktree-local helper, not committed)
# or per-suite:
build-dev\test_<suite>.exe --reporter compact
```

Compile evidence: full Release build of qgis_core + qgis_gui + sicnu_jobs +
sicnu_task_center + sicnu_data + sicnu_operators + sicnu_processing +
sicnu_agent + all 15 in-scope test executables, -j2, no warnings-as-errors
failures (transient cl.exe crashes on this host retried clean).
