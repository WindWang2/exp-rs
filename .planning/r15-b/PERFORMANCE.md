# R15-B 性能测量（PERFORMANCE，B14）

> 只记录真实运行得到的数字。测量用例为隐藏 Catch2 用例 `[.][r15b][b14][perf]`（`tests/test_r15_B_geodata_persistence.cpp`，提交 `e89b9b51`），
> 默认不随 ctest 运行；规模由环境变量 `R15B_PERF_CATALOG`（目录记录数）与 `R15B_PERF_RUNS`（实验 run 数）控制。
> 环境：box（Linux，替换前的机器），Release，`-j1` 串行运行，SQLite/GDAL 临时目录为本轨私有目录。数据全部为合成数据。
> 说明：以下数字在机器替换前测得，原始日志随 `/tmp` 丢失；数字取自当时的运行输出记录，未在新机器上复测（见 NOT VERIFIED）。

## 1. CatalogService 内存目录分页（100k 记录）

| 指标 | 第 1 次 | 第 2 次 |
|---|---|---|
| 构建 100k 记录 | 28.9 ms | 35.3 ms |
| 分页遍历（200 页，每页 500） | 45.0 ms | 43.0 ms |
| 平均每页 | 0.22 ms | 0.22 ms |

RSS：38,972 KB（起始）→ 81,288 KB（构建后）→ 峰值 89,452 KB。分页本身对 RSS 无明显增长；B05 新增的 `visitedRemotePages` 仅在远端续页上记账，本地分页不受影响。

## 2. ExperimentStore 写入/分页（20k runs）

| 指标 | 值 |
|---|---|
| upsertRun 写入 20k | 60.3 s（约 331 run/s） |
| listRuns 分页读取 41 页 | 共 375 ms |
| 首页 / 末页 | 4.5 ms / 11.1 ms |
| RSS | 55,512 KB，全程平稳 |

## 3. ExperimentStore 写入/分页（100k runs）

| 指标 | 值 |
|---|---|
| upsertRun 写入 100k | 1,621.8 s（平均 61.7 run/s，随规模持续下降） |
| listRuns 分页读取 201 页 | 共 7.8 s |
| 首页 / 末页 | 11.8 ms / 63.6 ms |
| RSS | 55,512 KB，全程平稳 |
| 用例总耗时 | 27 分 10 秒 |

## 结论（P2，未修复）
- **PERF-1（P2）**：`upsertRun` 吞吐随存储规模退化（20k 时 331/s → 100k 平均 61.7/s）。疑似每次写入伴随的索引维护/一致性查询随行数增长；需 profile 后再定，超出本轨"最小修复"范围。
- **PERF-2（P2）**：`listRuns` 采用 OFFSET 分页，末页耗时线性增长（11.8 → 63.6 ms），全量遍历为 O(n²)。已有 `listRunsByCursor` 游标路径可作为替代；切换调用方属于 API 行为变更，未做。
- 内存：两类存储在测量规模内均无泄漏迹象（RSS 平稳）。
- B07 新增的 `stepFailed` 检查在每次扫描结束时只执行一次判断，对上述分页耗时无可测影响（未做 A/B 对比，仅为推断）。
