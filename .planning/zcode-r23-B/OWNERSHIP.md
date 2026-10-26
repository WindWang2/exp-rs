# R23-B OWNERSHIP — 权属与冲突矩阵

## 本轨可写（B 白名单内实际触碰）
| 路径 | 类型 | 说明 |
|---|---|---|
| src/operators/runtime/openvino_provider.cpp | 生产 | W1 恢复合并丢失代码 |
| src/operators/runtime/tensorrt_provider.cpp | 生产 | W1 一行成员修复 |
| src/operators/runtime/tile_inference_engine.cpp | 生产 | W2 前置门（r21-B #1673、r22 #1709 曾改此文件，均已合并；当前无 open PR 交叉） |
| src/experiment/reproduction_bundle_import.cpp | 生产 | W3（若实施） |
| src/experiment/experiment_store.{h,cpp} | 生产 | W3（若实施；r21-B 曾改，已合并） |
| tests/test_r23_b_provider_stub_compile.sh | 新测试 | W1 gate |
| tests/fixtures/provider_stubs/** | 新 fixture | W1 stub SDK 头 |
| tests/test_r23_b_derived_output_roles.cpp | 新测试 | W2 |
| tests/CMakeLists.txt | 共享 | **append-only**：两个独立注册块，push 前与 origin/master 逐字节比对兄弟注册块 |
| .planning/zcode-r23-B/** | 本轨 | 计划文档 |

## 只读
src/processing/**、src/analysis/**、src/operators/rs|gdal|otb|opencv|io|python、src/agent/**、src/app/**、src/sdk/**、pi/**、src/operators/framework/**（除 model_catalog* 只读引用）、UI/科学核/Pi 工具、.goal-loop-ledger.md（全局账本只读）。

## 冲突矩阵（push 前 fetch 复核）
- 起始时点 Open PR = 0 ⇒ 无直接文件冲突。
- 并行 r23-A/C（worktrees 在 ../exp-rs-r23-A-*）进行中：其写集未知，push 前以 `git diff --name-only origin/master...HEAD` 对照其分支名下文件集（若已 push）。
- 共享 tests/CMakeLists.txt：本轨仅追加两个自包含注册块（带 `# --- end R23-B ... registration ---` 终界标记）；若 master 前移，合并时保留双方注册块，禁止 ours/theirs 整块覆盖（#1714/#1715 教训）。
- tile_inference_engine.cpp：r22-inference #1709 最后触碰（已合并 96 小时+）；r21-B #1673 再前。若 B 域他轨并行修改同文件（未见），按 hunk 语义合并。

## 决策（DECISIONS 摘要）
1. W1 测试采用 stub-compile gate（新 ctest）而非尝试装 OpenVINO/TRT SDK：本机无 SDK、CI 亦无；-fsyntax-only + 最小 API 面 stub 是唯一可离线常态化的编译门。
2. W2 gate 触发条件用 `!heads.empty()` 而非 `headsDeclared`：测试与调用方直接构造 ModelInfo 时不设 headsDeclared（r21-B 测试即如此），以向量本身为准更符合语义且不破旧清单。
3. W3 若实施走 ExperimentStore 新增单事务 API，而非在导入器里开第二条连接/手写 SQL：单一 authority，兼容现有 Result 诊断协议。
4. 不修 #1558（共享 e2e 测试文件恒真断言）：断言域（recipes/tasks）属他轨写集，B 轨改共享测试文件正文违反本轨 append-only 纪律；已在 ISSUE_TRIAGE 记录证据供归属轨处理。
5. #1467（src/core-fork 簇）OUT_OF_SCOPE：白名单不含 src/core；即使 P1 也不越界。
