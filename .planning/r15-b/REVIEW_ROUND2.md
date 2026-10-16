# R15-B 评审 Round 2（复核与签收）

> **声明**：与 Round 1 相同，由编写代码的同一代理执行，**是等价替代，不是真正独立的评审**。

范围：`46791bc6..e89b9b51` 全部 src 改动（7 个文件）与测试改动；复核 Round 1 每条处置，重新阅读 diff，并重新跑测试。

## 1. Round 1 处置复核

| ID | Round 1 处置 | Round 2 复核 |
|---|---|---|
| R1-01 | 已修（c85f475e） | 消息含 "a publish is in progress or was killed"，且保留 `interrupted mid-publish` 子串；O-B13.1 断言通过 ✔ |
| R1-02 | 已修（c85f475e） | atomic_fs.h：`recoverDatasetGroup` 文档 "Recovers an interrupted multi-file publication…" 已在其声明之上 ✔ |
| R1-03 | 残留（TOCTOU） | 保持；记入 FINAL_REPORT 残留风险 |
| R1-04 | 残留（覆盖面） | 保持 |
| R1-05 / R1-06 / R1-07 / R1-09 / R1-10 / R1-11 | 记录 | 保持 |
| R1-08 | LIVE_DEFERRED | 保持；LEDGER #16 |

## 2. Round 2 新发现

| ID | 级别 | 位置 | 发现 | 处置 |
|---|---|---|---|---|
| R2-01 | P3 | reproduction_bundle_import.cpp B12 checksums 解析 | 按 `\n` 切分，若 checksums.txt 为 CRLF，名字带 `\r`，合法文件会被判为 "not covered" 而拒绝迁移。这是失败即关闭（不会放行未校验文件），只影响可用性；导出端写 LF（reproduction_bundle.cpp:466），校验端同样按 `\n` 切分（:83），两者行为一致 | 记录，不修 |
| R2-02 | — | 同上 | 已核实：`verifyBundleChecksums` 在第 1 步无条件执行，失败即返回；所以"列入 checksums.txt"等价于"字节已校验" ✔ | 无需处理 |
| R2-03 | — | remote_identity_resolver.cpp | probe 失败后仍执行 `cache.drop(url)`，旧 token 不会被重用；revalidate 抛错返回空 token ✔ | 无需处理 |
| R2-04 | — | catalog_service.cpp B05 | 只对"会发请求"的远端续页（缓冲区空、href 非空）记账；本地分页与缓冲尾不受影响，O-B05.2 防误杀通过 ✔ | 无需处理 |
| R2-05 | — | experiment_store.cpp B07 | 8 处新增检查都在循环/单步之后；成功路径语义不变；`test_experiment_consistency_r4` 10/10 ✔ | 无需处理 |

## 3. 重新运行
HEAD `e89b9b51`，机器替换后重新构建：GREEN×2 = 113/113、113/113（见 TEST_RESULTS.md §3）；`git diff --check` exit 0。

## 4. 签收
- **P0：0；P1：0**。
- P2 残留：R1-03、R1-04、R1-08、PERF-1、PERF-2。P3 残留：R1-05/06/07/09/10/11、R2-01。
- 存在 NOT VERIFIED 项（resource_uri / range_cache thread、TSan、跨平台、B06/B11 未审计、新机器上未复测 RED 与性能），因此 **PR 以 Draft 提交**。
