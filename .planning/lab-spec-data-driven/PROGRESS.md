# PROGRESS — 会话日志

## 2026-09-12 会话 1

- P0 完成：审计落盘（EVIDENCE / CAPABILITY_MATRIX / PLAN / DECISIONS / GOAL / MILESTONES）。
  关键事实：operator 注册表 `AtomicAlgorithmRegistry`（110 个 `rs:*` + 6 个 `opencv:*`）；
  对话框执行 seam `runOperatorTask → JobRequest{algorithmId, params} → GuiJobHandle`；
  `resolveRuntimeDataPath("data/labs")` + `SICNU_DATA_DIR` 可复用；
  app 是单一 target `sicnu_geo_rs`；`sicnu_add_test` 提供 processing 全栈链接。
- P1 提交 `8051d7a162`：labspec.schema.json（draft-07，含 step 的 operator/action 互斥 +
  params 依赖 operator）+ `.gitignore` `!data/labs/**`（git add 实证通过）。
- 首次 configure 失败：FetchContent 在线克隆 pybind11 网络失败（SSL EOF ×3）。
  解决：`FETCHCONTENT_SOURCE_DIR_PYBIND11/CATCH2` 指向 main/build-dev/_deps 本地副本，
  重配成功。
- P2 代码完成（待构建验证）：`lab_spec_loader.{h,cpp}`（纯 QtCore+jsoncpp；
  结构校验镜像 schema；`resolveLabParamPaths` 注入式解析）；widget 重写
  （错误条目置顶、operator 步骤走 JobRequest、action 步骤 invokeMethod、
  10 个工厂删除）；CMake 接线（app 源列表 + 两个测试目标）。
  测试先行编写：`tests/test_labspec.cpp`（5 个 drift guard TEST_CASE）、
  `tests/test_guided_workflow_widget.cpp`（行为化重写：合法/非法加载、互斥规则、
  路径解析、shipped labs ≥10）。
- P3 提交 `7fc57ce967`：11 个 lab JSON（docs 7 ∪ widget 10 去重），全部过
  jsonschema 验证（Python 预检）；operator 参数面从对话框源码抄录实证。
  修复：schema operator_id 模式放行 `opencv:`；lab11 移除非法键 `operator_note`。
- P4 提交 `3980de6cf6`：`scripts/gen_lab_docs.py`（稳定序、无时间戳、--check 零 diff、
  手写页白名单），删除 7 个手写 lab 页 + 旧 README，重生成 12 页，--check 通过。
- P5 部分：`docs/labs/LABSPEC.md` + `docs/adr/0146-labspec.md` 提交 `bc4fecf145`；
  测试与 drift guard 代码已就位，等 `test_labspec` / `test_guided_workflow_widget`
  / `sicnu_geo_rs` 构建完成后跑 offscreen ctest。
- 构建（后台，-j2 + 60s 资源采样）：qgis_core 编译中，无 error；负载峰值 ~13（两路
  cc1plus 并行，RSS ~2GB，机器 62GB/16 核）。

## 错误记录

| 错误 | 尝试 | 解决 |
|------|------|------|
| zsh `--include=*.h` glob 报错 / `echo ===` | 2 | 引号包裹；不用裸 === |
| FetchContent 克隆 pybind11 SSL 失败 | 3 (cmake 内部重试) | FETCHCONTENT_SOURCE_DIR_* 指向本地 |
| Edit 工具要求先 Read（CMakeLists 用 sed 看过但未 Read） | 1 | Read 目标区段后重编辑 |
