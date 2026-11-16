# R31-T01 Path Ownership

Snapshot: 2026-11-15, base `85ce917bea0ece4df6548a677b97a72ecde8659b`; recalculate before commit.

## This track may write

Only call-chain-driven, minimal changes in scientific kernels/operators, model inference (except explicit T02 exceptions), data/dataset/geospatial, jobs/runtime/workflow, TaskCenter/OutputCommitter/public verification, `src/experiment/evaluation.*`, `src/app/shell/workflow_session_controller.*`, and `src/agent/cartography/cartography_operators.cpp` as an execution/thread adapter. New tests use `test_r31_t01_*`; shared CMake receives only this track's minimal marked block.

`src/processing/framework/tool_call_dispatcher.{h,cpp}` and `tool_call_dispatcher_task_center.cpp` are T01 files, but #2063 Python approval, identity, staging validation, owner-thread, conflict and duplicate-commit defenses are compatibility invariants.

## Explicitly excluded owners

- T02: all `pi/**`; `src/agent/runtime/**`; `src/sdk/exprs/external_process.*`; `src/operators/runtime/python_environment.{h,cpp}`, `python_script_operator.{h,cpp}`, `python_environments.qrc`; Agent session/copilot/AgentOps/catalog/host and managed-Python-specific implementation.
- T03: general GUI/Workbench/data-management UI, active view/layers, mapspec/symbology/layout, translations/help, and `src/app/classification/rs_post_process_task.cpp` UI cancellation wiring.
- T01 does not route every spatial/layout provider into the dispatcher, create another scheduler/committer/verifier, or modify vendored QGIS without a proven unadaptable production need.

## Active-exclude: open PR #2061 at `35f7a622`

All business files below are read-only while #2061 remains open. No cherry-pick, branch modification, or reimplementation of its experiment/teaching task.

- `docs/experiments/r30-t07-studio-teaching-guide.md`
- `docs/experiments/r30-t07-verification-and-review.md`
- `src/app/experiment_studio/experiment_studio_dock.{cpp,h}`
- `src/app/teaching/guided_lab_workspace.{cpp,h}`
- `src/app/teaching/lab_cockpit_dock.cpp`
- `src/app/teaching_admin/teaching_admin_dock.{cpp,h}`
- `src/app/workbench/dataset_experiment_panel.{cpp,h}`
- `src/experiment_studio/CMakeLists.txt`
- `src/experiment_studio/run_matrix_metric.{cpp,h}`
- `src/experiment_studio/studio_export.{cpp,h}`
- `src/experiment_studio/studio_json_numeric.h`
- `src/experiment_studio/studio_session.{cpp,h}`
- `src/experiment_studio/studio_session_io.{cpp,h}`
- `src/experiment_studio/studio_submission.{cpp,h}`
- `src/teaching/CMakeLists.txt`
- `src/teaching/lab_completion_ledger.{cpp,h}`
- `src/teaching/lab_draft_store.{cpp,h}`
- `src/teaching_admin/CMakeLists.txt`
- `src/teaching_admin/feedback_delivery.{cpp,h}`
- `tests/test_r30_t07_core.cpp`, `tests/test_r30_t07_gui.cpp`

Shared exception: `tests/CMakeLists.txt` permits only an independent minimal R31-T01 registration block; existing blocks are not edited. `src/experiment/evaluation.*` remains T01 because it is not in #2061's file set.

## Other shared files

- `.gitignore`: only the unique R31-T01 Markdown allowlist block appended for this ledger.
- Root/app/CLI/main-window CMake or hosts: no change unless a concrete T01 call chain requires a minimal owned insertion.
- Re-fetch open PRs and recompute their file sets before final rebase/PR. New open PR business files become active-exclude under the same rule.
