# 方向 C 文件所有权（R9）

## 本方向拥有（可修改）
- src/faultlab/** (教学 faultlab)
- src/app/widgets/**, src/app/teaching/**, src/app/teaching_admin/**, src/app/visualanalytics/**
- src/teaching_admin/**
- src/app/shell/gui_job_adapter.* (UI job 适配)
- tests/test_faultlab*.cpp, tests/test_scan_pool.cpp, tests/test_guided_workflow*.cpp,
  tests/test_teaching_admin_*.cpp, tests/test_gui_job_adapter.cpp
- tests/CMakeLists.txt (append-only 最小 diff，本方向拥有公共测试 CMake)

## 不拥有（禁止修改）
- A: 算法/科学核 src/analysis, src/operators(除 UI 契约侧), src/geospatial, src/dataset, src/experiment
- B: TaskCenter/Workflow/MCP/SDK/plugins/src/agent
- src/processing/framework/task_center.* — 只读调用（B 所有）；C1-2 修复不得改 TaskCenter

## 冲突矩阵（path + symbol + 来源）
| 文件 | symbol | Issue/PR | 方向 |
|------|--------|----------|------|
| src/app/widgets/rs_scan_pool.* | isStale/forgetOwner/cancel | #1453/#1474-T2 | C |
| src/teaching_admin/script_adapters.* | resolveDefaultPythonInterpreter/verifyOfflineBundle/checkLabPackDrift | #1466/#1474-T3/T4 | C |
| src/faultlab/fault_transforms.cpp | applyTrainTestSpatialLeakage | #1464/#1474-T0 | C |
| src/app/widgets/guided_workflow_widget.cpp | runOperatorStep/showStep | #1453/#1474-T1 | C |
