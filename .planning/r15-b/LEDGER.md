# R15-B Oracle-Ledger

> 续跑指引：分支 `hardening/r15-B-geodata-persistence`，构建树 `build-r15-b`；按本表最后一个"进行中/待办"继续。每个 WP 完成后 commit+push。

| # | WP | 状态 | RED 证据 | GREEN | 提交 |
|---|---|---|---|---|---|
| 1 | Step1 实时核验 | 完成 | — | — | docs |
| 2 | 基线构建+测试 | 完成：89/90（resolver 用例失败，#1504 回归） | baseline_ctest | — | — |
| 3 | B13 读侧中断发布闸门 | 完成 | Raster/Vector 用例 `refused=false`、无异常 | GREEN×2 | cec9b8d5 |
| 4 | B05 searchAll 去环 | 完成 | returned=50 unique=2 requests=50 | GREEN×2 | 46e321d0 |
| 5 | B04 resolver 失败即空 token | 完成 | 抛 `private / loopback … blocked` | GREEN×2 | f6e720a2 |
| 6 | B12 bundle data/ 迁移 | 完成 | escape.txt、unlisted.bin 被迁移，relocatedFiles=2 | GREEN×2 | f840caf2 |
| 7 | B16 #1481/#1505 残留 oracle | 完成（全部已在 master 修复的项通过） | n/a（oracle 首跑即绿） | — | b4310e60 |
| 8 | B07 ExperimentStore 扫描错误传播 | 完成 | 损坏叶页后 listRuns 返回 40/60 行且 success | GREEN×2（113/113） | 853af4c6（RED）、ba9487df（修复） |
| 9 | B02 full==tile==block 拼接 oracle + #1485 字面量 oracle | 完成（首跑即绿，无缺陷） | n/a | GREEN×2 | 7d2ac030 |
| 10 | Review Round 1 + R1-01/R1-02 修复 | 完成 | — | GREEN×2（113/113） | c85f475e |
| 11 | B14 隐藏测量用例 + REVIEW_ROUND1 | 完成（PERFORMANCE.md） | — | — | e89b9b51 |
| 12 | 机器替换后恢复 | 完成：重装工具链（含 qt6-5compat-dev），重新配置 `build-r15-b`，HEAD `e89b9b51` 重新 GREEN×2（113/113，两次） | — | GREEN×2 | — |
| 13 | TEST_RESULTS / PERFORMANCE / REVIEW_ROUND2 / FINAL_REPORT | 完成 | — | — | 本提交 |
| 14 | master 冲突检查 + Draft PR | 完成：origin/master 仍为 `46791bc6`，无需 rebase | — | — | 见 FINAL_REPORT |
| 15 | B06 / B11 审计 | 未做（LIVE_DEFERRED，时间预算） | | | |
| 16 | DatasetStore 10 个裸 QVector 扫描 + ExperimentStore 游标/lineage 扫描（R1-08） | 未做（需改 API） | | | |
| 17 | upsertRun 吞吐退化 / OFFSET 分页 O(n²)（PERF-1/2） | 未做（P2） | | | |
