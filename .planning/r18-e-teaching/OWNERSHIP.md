# R18-E OWNERSHIP — 写域与禁区

## 本轨独占写区

- `src/teaching/**`, `src/teaching_admin/**`, `src/lab/**`, `src/lab_pack/**`
- `src/grader/**`, `src/verification/**`, `src/verify/**`, `src/verify_adapters/**`
- `src/faultlab/**`
- `src/experiment_studio/**` 中**不涉及** experiment store/dataset 数据库 schema 的投影/会话/导出业务逻辑
- `src/help/**` 现有诊断与引导知识层
- `src/cli/lab_batch_runner.*`、`src/cli/cli_lab_commands.*`（CLI 批量/评分入口，教学业务面；最小修改）
- 测试: 新增 `tests/test_r18_e_*.cpp`；扩充本域既有测试文件（test_teaching_admin_core 等）仅在其语义属于本域时
- `.planning/r18-e-teaching/**`

## 只读/禁区

- `src/app/**`（桌面 UI/Qt 绘制，R18-D 写域）— D1 dock 接线、H8 dock 渲染、studio dock 文案均只写 CROSS_TRACK_FINDINGS.md
- `src/experiment/**`, `src/dataset/**`, `src/data/**`, `src/geospatial/**`（原 #1513 写集，已合并；仍非本轨范围）
- model runtime（R18-B）、science 算法 `src/analysis/**`（R18-A/#1534）、`src/agent/**` planner/MCP（R18-C）
  - 例外说明: `src/agent/output_verifier.cpp` 是 lab 评分权威（本域业务）。本轨**不改**该文件；rubric 契约不一致（D5）在 `src/teaching_admin/rubric_builder.cpp` 侧对齐 grader 权威规则。
  - `src/agent/harness/curriculum_progress.*` 只读引用（D1 契约建议写在本轨文档，不改该文件）。
- 共享 `tests/CMakeLists.txt`、根 `CMakeLists.txt`、公共 `src/agent/CMakeLists.txt`、`src/operators/CMakeLists.txt`、公共 manifest、根 README: 默认只读。新增 `tests/test_r18_e_*.cpp` 需要在 `tests/CMakeLists.txt` 注册时，只做最小独立块追加（沿用 sicnu_add_test 等既有函数），PR 中单独标出。
- Issues #1467/#1491: 非本轨，仅引用。
- PR #1534 写集: 零交集，无需避让。

## 每轮 Oracle-E 自检

`git diff --name-only origin/master...HEAD` 逐文件对照上表；共享 CMake 改动须证明不可替代并单独标出。
