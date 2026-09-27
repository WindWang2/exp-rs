# PR BODY（草稿）— fix(experiment,capsule,debugger,study): R4 experiment consistency — close the #1333 known-limitations list with typed failures and countable oracles

## 实测基线与分支

- 基线：`origin/master = 15e5c66b543ef3874cb929f17529ef456bd6c059`（PR #1333 合并点，2026-09-27 实测，与任务书一致；无漂移）。
- 分支：`hardening/r4-experiment-consistency`，worktree `exp-rs-experiment-consistency-r4`。
- open PR 盘点（开工时 7 个：#1334–#1340）：**零源码重叠**。唯一共享文件 `tests/CMakeLists.txt`（#1334/#1335/#1339/#1340 也在改）——本轨沿用仓库 append-only 惯例只追加注册块，冲突面为一处文本相邻，rebase 取并集即可。

## 任务与实际内容的关系

任务建议标题为"Track 11 Experiment Consistency R4 Deep Edition"。本 PR 实际内容 = #1333 已知限制 12 条的逐条处置（11 修 + 1 fixture 级收窄）+ 一致性 oracle 套件 + studio↔store parity + store 事务边界边界钉死，标题如实反映模块族。

## 12 条未做事项处置表

| # | 处置 | 提交 | 回归测试 |
|---|---|---|---|
| ⑤ outgoingEdges LIMIT 先于 edge-kind 过滤 | `outgoingEdges` 增可选 edgeKind/toKind 参数在 SQL 内先过滤后限页；`runsForCell` 直分页 recorded/run | 2ba843aa30 | consistency_r4: "runsForCell filters edge kind before the page limit" |
| ⑥ 50-twin 上限无标记 | `kMaxTwinScan` 常量 + `Verdict.twinScanCapped` + `twin_scan_capped` JSON + 可数 reason | a2684e0104 | consistency_r4: "repeat verdict marks when the twin scan hits its cap"（50/49 双侧边界） |
| ④ runsForCell 静默截断 | typed 拒绝 `experiment.matrix_cell_runs_overflow`（limit+1 探测）；ledgerForMatrix/aggregate/analyzeStudy/空间基线全链传播；buildStudyReport 保签名，拒绝时 STOPPED 报告（stoppedReason 携带诊断、零行零计数） | 9ed48f0a4f | consistency_r4: "runsForCell refuses a cell whose recorded runs overflow the page" + study_analysis: "analyzeStudy refuses typed when a point's ledger page overflows"（1001 边端到端） |
| ② benchmark 重复行 | 拒绝（D1），typed `experiment.benchmark_duplicate_sample` 带冒犯 id | 455fed2857 | benchmark_r4: 3 例（重复 truth/重复 prediction/OA=0.75 手算控制组） |
| ① flattenMetrics 结构不对称 | 数组路径收集 + `structure_mismatch:<path>` / `array_excluded:<path>` typed notes（上界 16+计数行），对称形状零噪声 | 218eb5fc94 | consistency_r4: "paired comparison names structural asymmetries..." |
| ⑨ lineage 100k 截断 API 化 | `LineageEdgePage{edges,total,truncated}` + `lineageEdgePage()`；`allLineageEdges` 为兼容包装；graph → `LineageQueryResult.experimentSourceTruncated`（JSON `experiment_source_truncated`） | 874eb6c0a2 + 1d05e18279 | consistency_r4: "lineage edge page reports truncation instead of hiding it" |
| ⑪ runById/promotionById fail-open | typed 读取器 `runRecordById`/`promotionRecordById`（`experiment.run_not_found` vs `experiment.run_corrupt`，promotion 同构）；判定路径全迁移：promotion 门拒 corrupt、ref 分类器区分 unreadable/vanished、debugger `kCodeRunCorrupt`、matrix 聚合拒绝；旧 optional 签名保留为展示接缝（corrupt→absent 已文档化） | 874eb6c0a2 + 7f62b2b0c9 | consistency_r4: "a corrupt run row is refused by the promotion gate, not read as absent"（含 absent 控制组） |
| ⑩ splitManifestsForVersion 静默跳过 | 文件内最小修复：跳过计数 + 稳定令牌 qWarning `dataset.split_manifest_corrupt_skipped`（D7） | 08c552f6a2 | consistency_r4: "skipped corrupt split manifest rows are named, not silent"（直插损坏行 + 消息捕获） |
| ⑧ seed ≥2^63 负 JSON 数 | legacy 十进制 seed 仅 [0,2^53) 整数可导入；否则 typed 拒绝（D5）；seed_hex（#1326）不受影响 | 4504c14dea | consistency_r4: "legacy decimal seeds outside the lossless domain are refused"（1e19 拒/-5 拒/42 精确，checksum 重铸） |
| ⑦ candidatePaths toInt 未查 ok | `toInt(&ok)` 失败排除，不再冒充 attempt-0 | (见 ③ 验证提交) | test_experiment_debugger: "attempt directory with an unparsable number is not an attempt (r4)" |
| ③ markdown ≥4 反引号残余向量 | `jsonCodeFence` 单点助手（围栏 = max(4, 载荷最长反引号串+1)，CommonMark），5 处调用点统一；载荷字节不变 | (见 ③ 验证提交) | test_experiment_bridge_r4: 2 例（毒载荷 7 反引号围栏 / 干净载荷 4 反引号零漂移） |
| ⑫ capsule 生产 run 端到端重放 | fixture 级闭环落地；真实平台部分书面保留（r2 起延续） | e77443ddbc | test_experiment_capsule: "capsule replay closure through disk keeps identity and verdict (r4)" |

## 一致性 oracle（20 个，全部可重复运行）

（清单在 EVIDENCE.md §oracle；跨 5 个新测试文件 + 3 个既有文件扩展，见下）

- 新文件：test_experiment_consistency_r4（9 例）、test_experiment_benchmark_r4（3 例）、test_experiment_parity_r4（4 例）、test_experiment_txn_r4（3 例）、test_experiment_bridge_r4（3 例）。
- 既有扩展：test_experiment_debugger（⑦）、test_experiment_capsule（⑫）、test_study_analysis（④传播 + 空study 边界）。

## studio↔store parity（WP-C）

studio 视图模型（projectRunMatrix ← buildStudyReport ← 纯 store 读）与 store 真值（listRunsByCursor 游标独立遍历 / runById / runsForCell）在四组场景下严格相等：批回滚双侧同时不可见、行集 1:1、转移拒绝后重投影不变、投影指标 == 落盘 run JSON。

## 事务边界（WP-E）

metric 批首冲突全回滚（直查 sqlite 文件计数）、lineage commit 故障点零边残留 + 重试幂等、故障后 reopen 无半事务（WAL/ADR 0137）且可写。变异效力已证：删 conflict 分支 rollback → oracle 红。

## 用户可感知行为变化

- 超预算 cell / 损坏行 / 重复 benchmark 行 / 无损域外 legacy seed：从"静默错误结果"变为"typed 拒绝或 STOPPED 报告"。
- twin 扫描打满 50、lineage 大图截断、split 损坏行跳过：输出携带显式标记/警告。
- 其余路径（CLI 展示、合法输入、空分区）零行为变化。

## 本地验证（不等在线 CI）

- 配置：gcc-15 / Ninja / Debug / ENABLE_TESTS=ON（复刻 r3 栈；无包装脚本）。
- 全程 `-j2`、`CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`。
- 轻量道逐套件绿 + 重链载体 `ctest -R "experiment|capsule|debugger|study|scientific_state" -j1` 连续两轮全绿（日志见 EVIDENCE.md，回填）。
- 每条修复 RED 先行（stash/checkout 回退实现后断言变红），变异效力测试一条（WP-E）。

## 未解决项（移交对象明确）

1. DatasetStore::allLineageEdges 截断报告（同型 API，src/dataset 白名单外）。
2. splitManifestsForVersion Result 化（3 个白名单外调用方）。
3. src/cli 展示路径 corrupt→not-found 归并显示（无判定危害）。
4. ⑫ 真实生产 run 执行面重放（需平台）。
5. incomingEdges 同型 edge-kind 下推（无消费方受影响，未做零漂移改动）。

## 回滚

单分支 revert；无 schema 迁移；新增 API 全部为加法（默认参数/新方法/新字段），不破坏既有调用方。
