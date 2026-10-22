# R20-05 OWNERSHIP — 文件级写权证明

BASE = origin/master = `28e1891390`。每次提交前执行
`git diff --name-only origin/master...HEAD` 并逐项对照下表;跨轨发现仅记录 CROSS_TRACK_FINDINGS。

## 允许写清单(提示词 §3 白名单命中)

| 文件 | 命中规则 |
|---|---|
| src/teaching/course_home_view_model.cpp | src/teaching/** |
| src/teaching/lab_feedback_projection.cpp | src/teaching/** |
| src/teaching/lab_completion_ledger.cpp | src/teaching/** |
| src/teaching/verdict_stamp.cpp | src/teaching/** |
| src/teaching_admin/batch_assessment.h | src/teaching_admin/** |
| src/teaching_admin/batch_assessment.cpp | src/teaching_admin/** |
| src/teaching_admin/rubric_builder.cpp | src/teaching_admin/** |
| src/teaching_admin/CMakeLists.txt | src/teaching_admin/**(模块自身构建脚本;仅追加一条已存在同级模式的 sicnu_grader 链接) |
| src/app/teaching_admin/teaching_admin_dock.cpp | src/app/teaching_admin/** |
| src/verify/verify_context.h | src/verify/** |
| src/verify/verify_engine.cpp | src/verify/** |
| src/verify_adapters/gdal_grid_probe.cpp | src/verify_adapters/** |
| tests/test_r15_E_teaching_stats_honesty.cpp | 对应教学测试(恢复被 merge 删除的文件) |
| tests/test_r15_E_batch_refusal_accounting.cpp | 对应教学测试(恢复被 merge 删除的文件) |
| tests/test_teaching_admin_core.cpp | 对应教学测试(改钉 #1652-1 旧方言的段) |
| tests/test_teaching_authoring_failures_r4.cpp | 对应教学测试(同上) |
| tests/test_teaching_admin_rubric_weights_r16_c_t06.cpp | 对应教学测试(同上) |
| tests/test_verify_adapters_grid.cpp | 对应 verification 测试(追加用例) |
| tests/test_r18_e_completion_ledger.cpp | 对应教学测试(追加用例) |
| tests/CMakeLists.txt | 共享文件——**仅 append-only 带唯一标记小段**(R20-05 marker),不排序/不格式化他轨内容 |
| .planning/zcode-r20-05/** | 本轨账本 |

## 只读(跨轨/共享),发现仅记录

- src/app/widgets/guided_workflow_widget.cpp(#1536 → 04 轨)
- src/recipes/{lab_document,provider_interfaces}.cpp(#1607 同类 → 跨轨)
- src/agent/**、src/data/**、src/analysis/**、src/core/**、src/gui/**、docs/adr/**
- 根 CMakeLists.txt / scripts/(未改动)
