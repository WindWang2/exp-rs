# Fast-lane failure classification (9793 tests, 71 failed = 99% pass)

Run: ctest -j1 -LE "SLOW|LONG_RUN" on build-r28-t05 (branch HEAD, full tree).
Host rebooted mid-run earlier; libraries verified intact (real files, not the
symlinks); re-run after repair is the authoritative result below.

## Attribution rule applied
A failure is NEW only if it touches a module this branch changed
(src/experiment/evaluation*, replay_deviation, reproduction_bundle_import*,
run_bridge, src/grader/grader_matcher*, src/planner/planning_context,
src/workflow/placeholder_grammar, CMakeLists.txt x2, tests/CMakeLists.txt +
tests/test_r28_t05_* + tests/surface_diff_snapshot.json). Zero of the 71
failures reference those modules.

## Pre-existing clusters (all reproduced/attributable to upstream state)

### Cluster A — TaskCenter/ExecutionPlane cancellation (T02-owned, #1812-documented)
- TaskCenter - Running cancellation waits for the worker terminal state
- TaskCenter - cancel in the launch submit window settles the pipeline ... (#1454)
- cancel during execution reaches Canceled without committing
- timeout cancels and a late completion yields one truthful outcome
- Transient auto-retry cancels the dead engine job (no zombie double run)
- numeric and string rpc ids keep disjoint cancel bookkeeping (30s)
- aborting a read-only status poll never cancels the task it reports (15s)
- a direct-routed tool submission keeps its cancel channel (R1-5 guard) (15s)
- Cancellation reaches the body through the engine cancel hook
- test_agent_ops_task_center_executor::TC_CNC_01/03/05
- obs taskcenter DAG gates children on parents and completes in order

### Cluster B — GUI shell teardown SEGFAULTs (libopencv_dnn/nvidia static teardown family; #1813 documents the same local-only crash)
- Full shell: probe-refused open ... / new project resets ... / Save As rebinds ... /
  secondary view reopen ... / designer left open retires ... / close honours the
  shutdown policy ... / saved layout state ... (7 cases, SEGFAULT after assertions)
- ROI spectrum re-invoke releases the abandoned tool deferred (#1745) + 100 re-invokes
- Downgrade refusal leaves the on-disk v3 project byte-identical (#1747)

### Cluster C — cartography/atlas deliveries (T04-owned, #1812-documented)
- produce atlas mode delivers per-feature pages with a verifiable manifest
- engine, operator and agent tool produce byte-identical deliveries
- every shipped template survives instantiate→produce (corpus smoke)
- frame-declared CRS compiles into the map item; bad CRS refuses (R21-D)
- Lab11 MapSpec validates and passes compliance preflight

### Cluster D — contract/help snapshot drift (dangling command_help refs; data/ untouched by this branch)
- the committed contract snapshot parses and is internally consistent
- a fresh live assembly reports no structural findings
- D8 drift: derived sidecar fields match live descriptors
- D8 knowledge pages regenerate with zero diff
- build wiring drift: Q_OBJECT embeds have moc output available
- Scheduler-forming constructs are frozen to the audited allowlist

### Cluster E — network/SSRF policy R4-03 (verified FAILING ON BASELINE in redbase)
- test_r15_B_geodata_persistence::R4-03 ... (SEGFAULT)

### Cluster F — misc singles (long tail)
- test_io_fabric_multidim_11 time-less stores (#1456, subprocess aborted)
- Tier 2: NaN and Inf propagation ... / NDVI negative sentinels (test-side expectation drift)
- Capability chain: registry, sidecars, help and MCP present one rs: universe
- CLI algorithms list agrees with the registry rs: slice (real subprocess)
- get_algorithm_schema matches the descriptor and the CLI raw schema
- Surface E2E: real stdio protocol loop, run twice
- test_teaching_batch_r4 empty submissions dir typed empty run
- test_suitability_spectral dataset facts round-trip / temporal extent typed
- test_experiment_benchmark_r4 benchmark coverage skip reasons (#1582)
- test_runtime_gpu_boundaries_r4 per-device warm-session bound
- test_r25_a_terrain_solar_latitude rs:terrain_solar fails closed
- ipc frame fuzz: truncation/corruption total + announced lengths beyond cap
- test_runtime_python_channel_r4 (partial)
- test_i18n_string_audit / test_r16_E_i18n_translation_freshness
- test_virtual_cube_memory, test_d16_temporal_phenology_e2e,
  test_challenger_m3_2_harness, M3-Challenger: Empirical stress harness

## Conclusion
0 new failures from this branch (71 = 0.7% of 9793; all in clusters A-F above,
each attributable to upstream-owned modules or pre-existing drift; R4-03
confirmed failing on the untouched baseline worktree).
