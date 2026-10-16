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
| 15 | B06 元数据事实链 + B11 空间划分泄漏审计 | 完成：B06 修复；B11 审计无引擎缺陷，补回归 | B06 RED（07e2554b）：bandCount 1≠3、8×8≠16×12、垃圾文件 state=Ready；B11 首跑即绿 | GREEN×2（22/22） | 790d302c |
| 16 | 第 5 项存储扫描吞错 + 第 4 项 #1449 close() 生命周期 | 完成 | RED（07e2554b，-DR15B_LEGACY_API）：allLineageEdges 32/60、outgoingEdges 40/60、allTags 40/60 且无错误；三个 race-close 用例 SIGSEGV | GREEN×2（精确提交 c68d5e95 上 8/8 两次） | c68d5e95 |
| 17 | PERF-1/PERF-2 | PERF-2：keyset 改动实测更慢（listRunsByCursor 每页 COUNT），已撤回，只提交测量用例；PERF-1 根因确认（run_ids 镜像重写），需改数据模型 → LIVE_DEFERRED | 实测见 PERFORMANCE §4 | — | 本提交 |
| 18 | 第 7 项 checksums.txt CRLF | 完成 | RED：完好 CRLF bundle 被判 checksum mismatch: manifest.json | GREEN×2 | d9ea2b01 |
| 19 | 第 7 项 journal 闸门扩展 + 开后复查 | 完成（开后复查只缩窄窗口，无确定性 RED） | RED：inspectRaster/inspectAny/validateCog/translateRaster 在有 .publish-journal 时成功 | GREEN×2 | c4ed7d85 |
| 20 | B15 远端源校验身份 | 完成 | RED：重签名 URL 的 recordedToken 为空、isStale=false、签名与 AKIA 凭据出现在 SQLite 文件中 | GREEN×2 | adb6e525 |
| 21 | 第 8 项 #1505 已合并 | resource_uri：T6/T7/T8/T9/T19 FIXED_ON_MASTER，#1505 T9 修复（bfe83d41）；range_cache T6 修复（7ed6dc02）；DNS T3 修复（fc712c72）；T11 跨所有权未做（POST_1505_PATCHES §C） | T9/T6/T3 RED 见 TEST_RESULTS §5.1 | GREEN×2 | bfe83d41、7ed6dc02、fc712c72 |
| 22 | 第 9 项 sanitizer + 第 10 项 RED 复跑 + 重型回归 | 完成：TSan 1 条真实 race→修复（f8e1f280）后 0；ASan+UBSan 0；原始 5 修复与本轮修复 RED 全部复现；回归 211/211×2，platform7/evaluation 二进制×2 | 见 TEST_RESULTS §5 | GREEN×2 | f8e1f280 |
| 23 | REVIEW_ROUND3 + 文档 + PR body | 完成；P1 #1505 T11 未关闭 → PR 保持 Draft | — | — | 本提交 |
