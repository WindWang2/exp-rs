# FINAL_REPORT — r9-science-data-integrity（方向 A，第一波）

- PR：https://github.com/WindWang2/exp-rs/pull/1475（OPEN，base=master，head=hardening/r9-science-data-integrity @ 6f1c3b858，33 文件）
- base SHA：8a08642760e2e8ffd2c4b965fc4683c01d3e4758（推送前重新 fetch 确认无新提交/无开放 PR，分支即最新基线）
- 不自动合并；未等待线上 CI（全部验证为本地可复现）。

## 已闭环 Work Packages（9 个代码 + 1 个文档）

W1 辐射状态 POSIX 冒号路径（#1469 review/#1467/#1450）；W2 模型 manifest SAR 定标态（#1472 review/#1450）；W3 file://localhost 伪 SMB（#1472 review）；W4 temporal 通用参数规范化（#1472 review）；W5 VerifyFailed 后 assetChanged（#1473 review/#1457）；W6 removeSample prepare 错误捕获（#1473 review）；W7 共享 fileExists 路由（#1472 review，跨界最小）；N1 fabric planner 截断信号（#1456）；N2 suitability digest 单权威（#1450）；N3 stepFailed 收口尾巴（#1448/#1457，含 workspace_catalog open() schema 探测 fail-closed）。

## Issue 对照

- 采纳并修复子项：#1469（1 thread）、#1472（4 threads）、#1473（2 threads）、#1456（query_planner）、#1450（suitability digest + kStates）、#1457（stepFailed 尾巴 + W5/W6 同文件）
- 核对后 ALREADY_FIXED_IN_MASTER（不重做，证据见矩阵）：#1461 全部 8 子项、#1458 全部、#1447/#1464 全部、#1448 范围内全部、#1449 范围内全部、#1456/#1457/#1467/#1450 其余子项
- NOT_A_SCOPE（B/C，记录）：#1469 agent/sdk 4 threads、#1472 run_exe.cmd、#1448 workflow/teaching 项、#1449 sdk/runtime/science_context 项
- 相邻问题记录待后续：rs_classification_pipeline.cpp:165（A 相邻 VerifyFailed 布尔吞掉）、agent_loop/session_journal.cpp:62、agent/harness/evidence.cpp:60（B）、asset_state_provider 无锁读写（C 协调）、SHA-256 fork 合并、spatial_split 分层债、busy_timeout-before-probe（R2 建议）

## 验证成绩（本地 MSVC lane）

- configure exit 0；7 定向测试 target 构建 exit 0（-j2）
- 双遍全绿：101 用例 / 1400 断言 ×2（7 套件明细见账本第 13 轮），全部 exit 0
- CTest 正式记录：5 关键用例 5/5 Passed（exit 0）
- Red 补证：W2/W3/W5/W6/N1 runtime red（exit 42 精确断言）、W1 编译红 + 独立复现器（5/17→17/17）、N2 如实记为契约锁、W4/W7/N3 代码级证据 + 对抗 review

## 双轮独立 review

- 第一轮（未参与实现，含 Qt 6.8.0 独立探针）：P0×2/P1×1/P2×7 → 全部 P0/P1/P2 处置闭环（P0-1 的原始 Windows 假设被实测证伪后重写为 setHost 统一方案）
- 第二轮 delta（第三个 agent，未参与实现；用项目原始编译命令重放全部增量 TU 零警告 + 探针复验 localhost 矩阵）：**MERGEABLE，零 P0/P1**；2 条建议性 P2 已记录

## 未验证范围（如实声明）

- Linux/macOS lane：POSIX 冒号文件端到端、file://localhost POSIX 拼写 → NOT_VERIFIED（平台感知断言 + Qt 文档行为 + 双 reviewer 探针佐证）
- ASan/UBSan/TSan lane：本机未配置，未运行
- 线上 CI：按协议未等待

## 下一波建议（remaining backlog）

1. rs_classification_pipeline.cpp:165 VerifyFailed 布尔吞掉（A 范围，需 bool→状态契约设计）
2. SHA-256 ≥7 fork 的 leaf 共享头文件化（faultlab/grader/preflight 三份先行）
3. asset_state_provider setResolver/resolve 无锁读写（与 C 确认 owner 后补锁）
4. science_context broker cache key typed-cause（#1465 涉及部分，需 C 协调）
5. spatial_split/temporal_cube 从 processing 目标剥离 + bufferedBlockFolds 双实现收敛（#1467 分层债）
