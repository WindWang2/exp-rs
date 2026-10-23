# R20-05 TASKS — Work Package 状态

状态:TODO / IN_PROGRESS / VALIDATED / UPSTREAM_FIXED / BLOCKED_CROSS_TRACK / N/A

| WP | 内容 | 状态 | 文件 | 验证命令 | 结果 |
|---|---|---|---|---|---|
| WP-ML | merge c8f5e0805422 教学侧丢失恢复(course_home %,feedback indeterminate,fail-closed batch loader,dock 两调用点,2 测试文件) | IN_PROGRESS | src/teaching/course_home_view_model.cpp, src/teaching/lab_feedback_projection.cpp, src/teaching_admin/batch_assessment.{h,cpp}, src/app/teaching_admin/teaching_admin_dock.cpp, tests/test_r15_E_{teaching_stats_honesty,batch_refusal_accounting}.cpp, tests/CMakeLists.txt(append) | 直跑 2 二进制 -j1 | 待构建 |
| WP-RB | #1652-1 rubric 校验委托引擎 GradingRubric::fromJson | TODO | src/teaching_admin/rubric_builder.cpp(+CMake link), tests 3 处 | test_teaching_admin_core 等 | 待 |
| WP-SV | #1600 网格采样证据盖章(hasSampling/sampledPoints) | TODO | src/verify/verify_types.h(GridInfo 在 verify_adapters), src/verify_adapters/gdal_grid_probe.*, src/verify/verify_engine.cpp, tests/test_verify_adapters_grid.cpp | test_verify_adapters_grid | 待 |
| WP-JP | #1607 残余:lab_completion_ledger + verdict_stamp jsoncpp pin+catch | TODO | src/teaching/{lab_completion_ledger,verdict_stamp}.cpp | test_teaching_* / test_lab_* | 待 |
| E01-E14 其余 | 见 ISSUE_TRIAGE;已修项做保护性验证 | 按需 | — | — | — |

## 每可疑点 oracle 记账(WP-ML)

| 可疑点 | source file | 触发案例 | 失败 oracle | 最小修复 | 双次复跑 | 基准/风险 |
|---|---|---|---|---|---|---|
| overall_percent=105 裸渲染 / stale 0 | course_home_view_model.cpp:137 | manifest+progressSummary overall_percent:105 | vm.overallPercent==100(钳制)且从 facts 派生 | 恢复 side2 逻辑 | 待 | 无 perf 影响;派生仅在 labsTotal>0 时覆盖 |
| 证据缺 passed → 整实验未通过 | lab_feedback_projection.cpp:290 | grader JSON 无 passed 字段 | cr.status=="indeterminate",聚合不翻 fail | 恢复三分支 | 待 | normalizeStatus 同义路径已在用 |
| 截断 JSON → 「0 份作业」摘要 | teaching_admin_dock.cpp:558/618 | 写半截 .json 再构建摘要 | 摘要输出含 error 而非 0/0% | loadBatchReportFromFile | 待 | loader 拒绝时 UI 明示,不改发布路径 |
