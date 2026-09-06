# BASELINE — recorded 2026-09-06

## Repo state

- HEAD master: `58eb196baac721943dc4bbdb5f29f934cf209cce`
- Recent merges: #745 Project Workspace/Data Governance & Reproducibility 3.0 (ADR 0129),
  #743 Plugin SDK 3.0, #742 Multimodal SpatioTemporal 3.0, #741 Spatial Execution 3.0,
  #740 Pi Scientist & Cartography 3.0; fixes #731/#732 (resume/cache hardening), #726/#727.
- Open PRs: none. Open issues: 15 (#746–#760).
- Main worktree has uncommitted `tests/test_layout_tools.cpp` change (not ours; untouched).
- Parallel epics active (do not touch): desktop-ux-4, plugin-sdk-ecosystem-4,
  scientific-algorithms-foundation-4 worktrees/branches.

## Environment

- 16 cores, 62 GB RAM (≈37 GB available), 208 GB disk free.
- gcc `/usr/bin/c++`, Ninja, ccache present. Build policy: `CMAKE_BUILD_PARALLEL_LEVEL=2`,
  `CTEST_PARALLEL_LEVEL=1`, raise only with observed headroom; heavy suites sequential.
- Test env: `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib`; CTestCustom.cmake pins
  PYTHONHOME/PYTHONPATH/QT_IM_MODULE.

## Key directories / dependency boundaries

- `src/data/` — DataManager (asset authority, revisions), execution_fingerprint,
  artifact_store, artifact_object_pool, `governance/` (GovernanceStore SQLite WAL,
  WorkspaceService, workspace_lifecycle (Snapshot/Cleanup), ImportCenter,
  MetadataPipeline, RelinkService, ReproBundleExporter, WorkspaceValidator).
- `src/workflow/` — WorkflowRuntime/Session, WorkflowRunCoordinator (+checkpoint,
  run lock), ArtifactGC, placeholder grammar.
- `src/jobs/` — JobEngine (worker threads, JobRecord).
- `src/runtime/` — worker/, gpu/, observability/, chunk/ (isolated worker architecture).
- `src/processing/framework/task_center.cpp` — TaskCenter + execution cache seam
  (ExecutionResultCache inside task_center), ToolCallDispatcher.
- `src/app/` — data_project_serializer (project format v3), main_window_project.
- `src/agent/…/governance_tools.cpp` — project:* bounded tools.
- Tests: tests/test_governance_store, test_governance_tools, test_workspace_services,
  test_workspace_project_v3, test_workspace_snapshot, test_workspace_stress,
  test_artifact_store, test_workflow_* (recovery, resume_provenance, run_coordinator,
  cache_e2e, incremental_cache, artifact_gc, cancel), test_remote_source_cache,
  test_worker_host, test_temporal_workspace.
- Benchmarks: benchmarks/workspace-governance-3-100k.json (baseline numbers in CHANGELOG).

## Known defects at baseline

See GOAL.md issue table (#746 P1; #749/#750/#751/#752/#753/#754 P2; #758 bundle P3).
Additionally PROJECT.md is stale (describes the #708-#712 PR-integration sprint) — issue #760.

## Explicit out-of-scope

- Plugin SDK / native plugin lifecycle (#747/#748/#755/#756/#757).
- Temporal RMSE kernel (#759).
- UI redesign, new algorithm kernels, STAC feature expansion beyond identity/validator
  hardening.

## Baseline test/build facts

- Documented runner: `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest
  --test-dir build --output-on-failure`. Exact suite counts vary; no green claim made
  without a fresh local log.
- Existing build dir in main worktree: Release/Ninja (`build/`). Epic worktree builds in
  its own `build/` with ccache warm from main builds.
