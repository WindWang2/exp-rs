# R30-T05 LEDGER — 轮次记账

| # | 工作包 | 改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|---|
| 1 | WP00/01 | worktree zcode/r30-t05-pi-mcp-session-20261113T174105Z @ 606e3c9101；BASELINE.md 六条候选判定 | Pi 套件 66/65/0/1 复测；configure OK（OpenCV 共享前缀） | ✅ | WP02 RED |
| 2 | WP02 | RED: r30_t05_drift_dispatch.test.mjs 6 用例全败（无 gate，NaN 场景挂起证明无限轮询类问题） | node --test | 🔴 RED 确认 | 实现 gate |
| 3 | WP02 | mcp_bridge.ts +SCHEMA_DRIFT/SCHEMA_UNVERIFIED；exp-rs-spatial.ts: runComparison/gateDispatch/崩溃窗口重验；status +comparisonGeneration；fake server +delay/fail-tools-list | drift 测试 6/6 GREEN；全套件 72 中 3 个旧断言需按新契约更新 | 🟡 | 更新旧测试 |
| 4 | WP02/04 | r29_t03_schema_drift（helper 触发+恢复派发断言）、r29_t03_details_passthrough（诚实 continuation）更新 | 全套件 72/71/0/1 GREEN | ✅ 已提交 a0e0e56a? （见 git log） | WP03 |
| 5 | WP03 | RED: r30_t05_wait_semantics 8 用例（NaN 挂起=RED 证据，TaskStop 终止） | node --test 挂起于 NaN 死轮询 | 🔴 RED 确认 | 重写 wait |
| 6 | WP03 | wait 工具闭环：timeout 校验、AbortSignal.any 预算联动、abort 复查、isError 透传、malformed 3 连击、cancelling 非终态；fake server +status-script/call-log | wait 测试 8/8 GREEN | ✅ | WP04 |
| 7 | WP04 | structuredDetails: artifactEnvelopeFromTruncated（尾窗扫描+三元组门槛+转义安全）、structuredContent 4096/串上限、可执行 continuation | continuation 4/4 GREEN（修正 resultDroppedChars 语义=整个文档） | ✅ | 全套件复验 |
| 8 | WP02-04 | 全套件复验 | 84 tests / 83 pass / 0 fail / 1 skip | ✅ commit 1 | WP06 |
| 9 | WP06 | session_journal.cpp load 改为 cap+1 有界分块读（growing-file 分配有界）；新 tests/test_r30_t05_journal_read_bound.cpp（cap 内/外/跨块）+ CMake 注册 | 待构建（build1 进行中 26min@55%） | ⏳ | 构建+ctest |
| 10 | WP05 | test_mcp_server.cpp 追加 evict 边界用例（66 启动→首条 UNKNOWN_SESSION+reconcile、末条+id-less status=最新 final envelope） | 待构建 | ⏳ | 构建+ctest |
| 11 | WP11 | r30_t05_e2e_real_host.test.mjs（E1 真实结果链+>50k 分页 sha256 重组；E2 abort 服务端取消/timeout 不取消/host 存活） | 待 surface_mcp_host 构建 | ⏳ | 构建后运行 |

## 构建状态

- build1 ✅（100%，sicnu_geo_rs + surface_mcp_host + 11 个测试目标）
- build2 ✅（全部 WP05-10 验证目标）
- build5 ✅（test_mcp_server 重建 + surface host GDAL 修复）
- build7 ⏳（surface host 取消契约 fixture 修复，排队等他轨 host 锁）

## C++ 测试结果（本 HEAD，2026-11-14）

| 套件 | 断言/用例 | 结果 | WP |
|---|---|---|---|
| test_r30_t05_journal_read_bound | 13/3 | ✅ | WP06 |
| test_agent_loop_core | 216/17 | ✅ | WP06 回归 |
| test_agent_loop_journal_durability | 71/4 | ✅ | WP06 回归 |
| test_agent_loop_seams / modes / e2e | 101/10, 110/11, 150/15 | ✅ | WP06/07/08 |
| test_agent_ops_core / driver | 491/47, 62/5 | ✅ | WP07 |
| test_cli_agent_ops / test_agent_session_adapter | 61/5, 10/2 | ✅ | WP07 |
| test_mcp_server（全部） | — | ✅（除新用例外未重跑全量？已跑：新用例 141/1 ✅；全量待最终复验） | WP05 |
| test_r29_t03_production_driver | 86/7 | ✅ | WP05/07/09 |
| test_r27_t03_approval_gate / dock_lifecycle | 214/4, 12/6 | ✅ | WP07/09 |
| test_agent_ops_ui_smoke | 12/5 | ✅ | WP09 |
| test_r25_c_capability_honesty / workflow_resume_diagnostics | 198/1, 95/4 | ✅ | WP05/06 |
| test_r28_t03_production_seams / verification_matrix / shm_manifest_gates | 70/7, 17/1, 27/2 | ✅ | WP07/08/10 |
| test_harness_adversarial_matrix / test_r26_harness_evidence_integrity | 88/16, 71/8 | ✅ | WP08 |
| test_exprs_plugin_loader / plugin_host_process / channel_parity_r4 / lifecycle_unload_order_r4 / execution_barrier | 566/38, 371/34, 10/3, 34/4, 23/6 | ✅ | WP10 |

## E2E 结果

- E1 ✅（surface host 真实链：发现 199 工具、真实栅格 inspect（GDAL 修复后）、surface_noop 执行、wait 真实终态、>50k artifact 经 extension 分页重组 sha256 一致、截断页 continuation 信封）
- E2 ⏳（E1 同文件；window-1 取消终态待 fixture 取消契约修复重建后复验 — 见 HANDOFF T04 知会）
- E3-A ✅（kill→gate→checkpoint→resume 10.8s）；E3-B ✅（生产 binary UNKNOWN_SESSION + pre/post-plan reconcile 判定 + 幂等）
- r25_c_e2e_real_host：cancel 段暴露 fixture 取消契约缺陷（master 既有，本环境首次可运行）→ fixture 修复待 build7 复验

## 失败命令记录

- configure 首败：OpenCV_DIR 未设 → 共享前缀解决（D5）。
- build4/build6 exit 75（host 锁被 T04/他轨持有）→ 按协议排队（build5/build7 --wait 3000/5400）。
- r25 cancel 段失败（fixture 取消契约，非本轨引擎回归）→ 修复待复验。


## 审查链（最终）

- R1（独立子 agent）APPROVE-WITH-FIXES：2 阻塞（crash-boundary 覆盖、post-hoc 警告）+ 2 必修（gate 预算、docs）+ 6 注记 → 全部处置（REVIEW_R1.md）。
- R2（独立子 agent，绑定 HEAD）APPROVE-WITH-FIXES：N1 阻塞（headNote 计数）+ N2-N5 非阻塞 → 全部处置。
- R2 补充复核（同一审查者，delta commit ea27c41ca1）：N1-N5 全 RESOLVED、无回归、独立复跑 90/90 → **APPROVE**（REVIEW_R2.md）。
- 最终套件：Pi 90/90/0/0（多遍）；C++ 见上表；E2E E1/E2/E3 全绿。
