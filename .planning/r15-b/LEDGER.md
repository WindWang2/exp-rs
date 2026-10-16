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
| 8 | B07 ExperimentStore 扫描错误传播 | 进行中 | listRuns success 40/60 | 待构建 | — |
| 9 | 文档/评审/PR | 待办 | | | |
