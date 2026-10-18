# R16-D Ownership

## Work IDs and branches
- Integration: `hardening/r16-d-workbench-cartography`, worktree `../exp-rs-r16-d-integration`
- Track 1: `R16-D-T07` Qt/QGIS workbench — branch `hardening/r16-d-t07-workbench`, worktree `../exp-rs-r16-d-t07`
- Track 2: `R16-D-T08` MapSpec/Cartography — branch `hardening/r16-d-t08-cartography`, worktree `../exp-rs-r16-d-t08`

## Write ownership (enforced)
- T07: src/app/workbench/**, src/app/display/**, src/app/main_window_vector.cpp, src/app/main_window.h (additive member only), src/app/main_window_workbench.cpp (single lambda), src/app/shell/ribbon_controller.cpp (NOT in #1517 diff — verified), src/app/main_window_menus.cpp (tooltip seam only, minimal). Tests: extend existing registered files.
- T08: src/agent/cartography/**, src/agent/mapspec/**. Tests: extend existing registered files (test_mapspec.cpp, test_cartography_production_11.cpp, test_cartography_quality.cpp, test_platform5.cpp).
- Forbidden to both: #1517 files (src/app/shell/{rs_job_panel,task_panel_host,task_progress_text,workflow_session_controller}, src/app/widgets/rs_result_summary.cpp, src/app/teaching_admin/**, src/teaching/**, src/help/**, src/lab_pack/**, data/help/**), src/processing/**, src/operators/**, pi/**, src/experiment/**, src/geospatial/**, src/data/**, src/dataset/**, src/grader/**, tests/CMakeLists.txt.
- Cross-module reads allowed; new cross-module interfaces NOT added this round (all fixes stay within owned files; T07-F07 fix removes a mutation instead of adding an API).

## Parallel-write rule
Only one writer process per worktree at any time. Integration worktree is written only by the integration agent (me) after both tracks land their commits. Build lease: `flock /home/kevin/project/exp-rs-r16-build.lock` for configure/build; `CMAKE_BUILD_PARALLEL_LEVEL=4`, `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`.
