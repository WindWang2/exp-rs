# R21-C ORACLES — 硬性完成判据

## 本轨 Oracle（任务书 §7）
- **O-C1** 词汇表一致：Pi↔MCP↔SDK↔tool catalog 的 discover/schema/call/result/error 回归（R20 #1670 全部测试）两遍绿。
- **O-C2** fail-closed：多轮资产变化/授权撤回/取消重入/超时重试均 typed 拒绝，有测试非人工日志。
- **O-C3** Workflow：placeholder 单次替换（#1594 回归）、DAG 多步/缓存/resume 不重复 publish 既有测试两遍绿。
- **O-C4** malformed 输入：bad CLI flags（W-A/W-B 后）非零退出+typed error；malformed JSON/IPC、插件类型错宿主不 abort（#1551 回归 + W8-red 修复族）。
- **O-C5** E2E ≥4 条两轮；schema sidecars/手册再生 diff 清洁。
- **O-G1** 盘点可查（BASELINE.md/ISSUE_TRIAGE.md/PR_COLLISION_MATRIX.md）。
- **O-G2** 每项修复 RED/GREEN ×2 + 生产路径证明（RED_GREEN.md）。
- **O-G3** 性能基准与内存上界（PERF_BASELINE.md）。
- **O-G4** 受影响测试 + R1/R2 独立审查 PASS。
- **O-G5** known-baseline-red/skip/not-executed 逐项归因（TEST_RESULTS.md）。
- **O-G6** PR 文件 100% 在 OWNERSHIP 写区；无静默回滚（git log -S 抽查守卫点）。
- **O-G7** 无 TODO 假实现/占位 mock/绕过安全门/假数据 PASS。

## goal-loop 迭代纪律
每轮：读 LOOP_LEDGER → 差距 → 单一改动 → 验证 → 记账 → 判定。连续 3 次不进→换假设；连续 8 次→审前提。验证不通过不结束回合；完成需验证跑两遍。
