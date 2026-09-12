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
| master@27b9aa0a63 `auto*`←unique_ptr 编译失败（#913 引入） | 1 | 分支内 `.get()` 修复 55426a0797（DECISIONS #7） |
| `Qt.AlignCenter` Python-brain 手误 | 1 | `Qt::AlignCenter` d51cc162 |
| test_labspec `auto*`←shared_ptr；裸测试目标缺 src/app include 路径 | 各1 | `auto`；include 目录补 src/app |
| gui_job_adapter.h→task_center.h→qgstaskmanager.h 拖 qgis 头进裸测试 | 1 | PIMPL（aa66079 + df179ad） |
| `pkill -f 'build_and_log'` 自匹配杀掉自己的 shell | 1 | 换用更精确的进程选择，重跑命令 |
| `ctest -R test_labspec` 得 "No tests were found"（注册名是 Catch2 用例名） | 1 | 用用例名正则跑 ctest + direct 二进制运行，双证据 |
| `.git/info/exclude` 的 `.planning/` 使否定规则失效 | 1 | `git add -f` 指定 .md（先例一致） |

## 2026-09-13 会话 1（收尾）

- 测试门全绿：test_labspec 5 cases/91 assertions ✓；test_guided_workflow_widget 5/60 ✓；
  ctest 汇总 10/10 ✓；workbench 4 套件 23 cases ✓；sicnu_geo_rs 链接成功。
- 基础破损处理：master@27b9aa0a63 的 #913 引入 `auto*`←unique_ptr（任何编译器都病构），
  分支内最小修复 `55426a0797`（.get()），commit message 注明上游修复后可丢弃。
- 构建事故记录：整轮全量构建两次被编译错误打断（上述 base 破损 + `Qt.AlignCenter`
  Python-brain 手误 `d51cc162`），均为小修后 -j1 续跑；负载 24.45>1.5×16 时按硬约束
  从 -j2 降为 -j1。
- rebase：fetch 后分支已含 origin/master 尖端（27b9aa0a63），无需实际重放。
- push 成功（guardrail 未拦截，无需重试）；PR：https://github.com/WindWang2/exp-rs/pull/949
- 会话结束状态：未合并、未等 checks；worktree 保留至 merge 后移除。
