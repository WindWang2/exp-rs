# R15-A Issue / PR Review Matrix — 逐条分类（M0）

分类词表：`FIXED_ON_MASTER`（最新 master 已修，读码/合并 diff 证实）/ `COVERED_BY_MERGED_PR`（已合并 PR 的文件清单覆盖，待本轨编译/运行验证）/ `REPRODUCED_OPEN`（本轨复现，待修）/ `NEEDS_REPRO`（需构造最小输入验证）/ `NOT_REPRODUCIBLE`（无具体复现，记录后搁置）/ `OUT_OF_SCOPE`（非本轨所有权）。
**所有"已修"判定均以 BASE_SHA `46791bc6` 上的代码/合并 diff 为据，不轻信 GitHub 状态。**

## A. PR 侧（0 open PR；3 个快照 PR 全部已合并）

| PR | 状态 | 本轨动作 |
|---|---|---|
| #1505 | MERGED 2026-10-16 02:58:58Z | 其修改的 `src/core/temporal_cube.cpp`、python pool、SSRF、broker 均**不在**本轨可写范围；#1486/#1485/#1456 相应子项据此判 COVERED_BY_MERGED_PR |
| #1509 | MERGED 2026-10-16 02:59:21Z | `rs_spectral_index_operator.cpp` 避让锁解除；#1447/#1488 据此判 COVERED_BY_MERGED_PR；该文件 +4/-4 diff 待读（ORACLES O-audit） |
| #1512 | MERGED 2026-10-16 02:35:45Z | 无文件重叠 |
| #1506（R14-A） | MERGED 2026-10-15 | **dedup 基准**：10 个生产文件 + 7 个测试文件的修复禁止 revert；其 FINAL_REPORT 残留项转本轨候选（ORACLES O1–O3） |
| #1501/#1504/#1502/#1503 等 | MERGED（10-14） | R14-A 已验证其在 BASE_SHA 存在，不重做 |

## B. Issue 侧（25 open）

| # | 主题 | 分类 | 证据 / 理由 |
|---|---|---|---|
| 1499 | python_worker_process_pool.cpp 多余 `}` 编译断裂 | COVERED_BY_MERGED_PR | BASE_SHA 该文件命名空间仅 790 行一次闭合（#1505 改 +16/-5）；本轨 configure 时将顺带编译验证。非本轨文件 |
| 1495 | clang-tidy continue-on-error | OUT_OF_SCOPE | CI 门禁属 build/test 轨 |
| 1494 | test_challenger_m1_2_harness.cpp 孤儿测试 | OUT_OF_SCOPE | 测试注册属 test-infra；非科学算法 |
| 1491 / 1490 | master 无 CI 结果 / concurrency 组替换排队 | OUT_OF_SCOPE | CI 语义；且本轮明确不依赖线上 CI |
| 1489 | lupdate 只扫 src/app | OUT_OF_SCOPE | i18n/E 轨 |
| 1488 | canonical_json asInt64 抛 LogicError | COVERED_BY_MERGED_PR | #1509 标题即 "(#1447, #1488)"，`src/verification/canonical_json.cpp` +19/-5；非本轨文件 |
| 1486 | worker pool 无锁改写 node | COVERED_BY_MERGED_PR | #1505 改 `python_worker_process_pool.cpp/.h`；非本轨文件 |
| 1485 | SSRF 字符串判定可绕过 | COVERED_BY_MERGED_PR | #1505 "harden SSRF perimeter"，`offline_gate.cpp` +83；B 轨文件 |
| 1468 | Windows injected main _exit / fixture recv 超时 / netcdf | OUT_OF_SCOPE | CI/test 基础设施 |
| 1467 | core-fork cluster | 部分 COVERED_BY_MERGED_PR + 部分 NOT_REPRODUCIBLE | ①"scene-read 失败→NaN composites"经 R14-A 矩阵映射为 temporal_cube catch-all，#1505 已合并（`src/core/temporal_cube.cpp`，本轨只读）；②radiometric preflight GDALOpenEx：R14-A 在 BASE_SHA 读码证实 FIXED_ON_MASTER（`radiometric_state.cpp` 经 `local_source::isPlainLocalPath` 路由）；③"misc layering"无具体文件/行号，NOT_REPRODUCIBLE（搁置，待具体线索） |
| 1466 | teaching/suitability cluster | OUT_OF_SCOPE | E 轨 |
| 1465 | help/science_context cluster | 部分 COVERED_BY_MERGED_PR + NEEDS_REPRO | ①broker cache key 丢 typed failure：#1505 改 `broker.cpp` +36/-1、`broker.h` +14/-1 → COVERED_BY_MERGED_PR；②"four 3-arg std::equal 可越界读"：未定位文件，NEEDS_REPRO（若落在 `src/analysis` 或 `src/processing/algorithms` 则转 REPRODUCED_OPEN）；③terminology dedup：science_context/help，非本轨 |
| 1464 / 1463 / 1462 / 1460 / 1459 / 1458 | verify/grader/agent_ops/python-plugins/cli-sdk/workflow/experiment | OUT_OF_SCOPE | 分别属验证/教学/D 轨/C 轨 |
| 1456 | geospatial egress/cache | COVERED_BY_MERGED_PR | #1505 改 `offline_gate.cpp`/`range_cache.cpp`；B 轨 |
| 1455 | operators runtime/io cluster | OUT_OF_SCOPE | `src/operators/runtime/**` 为本轨禁写区（C 轨） |
| 1454 | processing task-center cluster（cancel-vs-submit race / committer temp-retention / logBuffer deep-copy） | NEEDS_REPRO（仅科学算子回调侧） | 三个命名子项均在 TaskCenter 所有权文件（非 `src/processing/algorithms/**`，本轨只读）。按 Prompt 规定**只审科学算子回调/计算副作用**：审计 TaskCenter 调用 rs 算子路径上的回调闭包完整性，不抢 TaskCenter 所有权 |
| 1449 / 1448 / 1447 | concurrency 析构 / stores fail-open / json 未类型化 | OUT_OF_SCOPE / OUT_OF_SCOPE / COVERED_BY_MERGED_PR | #1447 由 #1509 合并修复（标题明示）；另两个非科学算法 |

## C. 本轨候选缺陷簇（来源：R14-A 残留 + 上述 NEEDS_REPRO + 本轨审计）

见 `ORACLES.md`：O1（融合 resample-then-mask 边缘掺混）、O2（sam_classify vs spectral_similarity NoData 方言）、O3（#1465 3-arg std::equal 定位）、O4（#1454 科学算子回调侧审计）、O5（rs_spectral_index_operator.cpp #1509 后再审计）、O6+（A01–A16 工作包审计新发现，按复现准入）。

## D. 结论

- **零 open PR**：本轨无外部文件冲突；可全速执行。
- 25 个 open issue 中 0 个判定为"本轨 REPRODUCED_OPEN"（除审计新发现外）；多数为状态遗留（issue-closing 关联缺失）或其他轨所有权——与 Prompt 预判一致，**不做大批机械修复**。
- 本轨价值主线：R14-A 明确保留的残留项 + 逐函数边界审计新发现，全部走 RED→GREEN×2。
