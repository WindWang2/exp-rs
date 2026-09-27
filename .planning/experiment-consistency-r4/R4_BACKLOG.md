# R4_BACKLOG — #1333 十二条已知限制处置总账

状态图例：`PENDING` → `RED`(复现测试写好) → `FIXED`(提交号) / `KEPT`(书面保留)。
每条"现状证据"均为本轨道在 `15e5c66b5` 上独立复验（rg/sed 行号可复核），非抄 PR 正文。

| # | 条目 | 现状证据（独立复验） | 处置预案 | 优先级 | 状态 | 提交号/理由 |
|---|---|---|---|---|---|---|
| ① | `flattenMetrics` 结构不对称启发式 | comparison_ext.cpp:175（递归 :221，入口 :250-251）；嵌套对象/数组形状不同侧静默缺席 paired delta | 已修：数组路径收集 + structure_mismatch:/array_excluded: typed notes（上界16+计数行），对称形状零噪声 | P1 | FIXED | 218eb5fc94 |
| ② | benchmark truth/prediction 重复行静默重加权 | benchmark_runner.cpp:307 `truthById.insert` last-wins；:348 预测循环逐行 `matrix.increment` | 已修：D1 决策=拒绝，typed `experiment.benchmark_duplicate_sample` 带冒犯 id；控制组 OA=0.75 手算 | P1 | FIXED | 455fed2857 |
| ③ | markdown ≥4 反引号行残余向量 | experiment/bridge/lab_report_writers.cpp:200-265 用 ```` 围栏包裹载荷；载荷含 ≥5 反引号行可破围栏 | 已修：D6 决策=jsonCodeFence 单点助手（围栏=max(4, 载荷最长反引号串+1)，CommonMark），5 处调用点统一；载荷字节不变 | P2 | FIXED(实现)/验证待重链 | (提交号待③验证) |
| ④ | `runsForCell` 超 kMaxMatrixCells 静默截断 | experiment_matrix.cpp:304 `LIMIT` 直接绑定 limit（默认 1000）；超限无任何标记 | 已修：D2 决策=typed 拒绝 `experiment.matrix_cell_runs_overflow`（limit+1 探测）；ledgerForMatrix/aggregate/analyzeStudy/spatial 全链传播；buildStudyReport 拒绝时 STOPPED 报告 | P1 | FIXED | 9ed48f0a4f |
| ⑤ | `outgoingEdges` LIMIT 先于 edge-kind 过滤 | experiment_store.cpp:2001 起 SQL 先 LIMIT 后过滤 | 已修：outgoingEdges 增可选 edgeKind/toKind 参数在 SQL 内先过滤后限页；runsForCell 直接分页 recorded/run | P1 | FIXED | 2ba843aa30 |
| ⑥ | repeat_execution 50-twin 上限无标记 | repeat_execution.cpp:136 `runIdsByExecutionFingerprint( identityHash, /*limit=*/50 )` | 已修：D8 决策=kMaxTwinScan 常量 + Verdict.twinScanCapped + twin_scan_capped JSON + reasons 可数行 | P2 | FIXED | a2684e0104 |
| ⑦ | `candidatePaths` toInt() 未查 ok | debugger/evidence_source.cpp:506 `match.captured(1).toInt()` 无 ok 检查，失败计 0 参与排序 | 解析失败显式处置（跳过该候选或 typed 诊断，不参与排序） | P2 | PENDING | — |
| ⑧ | seed ≥2^63 负 JSON 数 | reproduction_bundle_import.cpp:211-216 无 seed_hex 时 `toDouble(0)` 强转 quint64，>2^53 失真、≥2^63 为负 | 已修：D5 决策=legacy 十进制仅 [0,2^53) 整数可导入，否则 typed 拒绝（警告命名 seed 键）；seed_hex bundle 不受影响 | P2 | FIXED | 4504c14dea |
| ⑨ | lineage 100k 边截断 API 化 | experiment_store.h:177 `allLineageEdges( limit = 100000 )`；store.cpp:1987 `qBound(1,limit,1000000)` | 已修：D4 决策=LineageEdgePage{edges,total,truncated}+lineageEdgePage()；allLineageEdges 保留兼容包装；graph→LineageQueryResult.experimentSourceTruncated 进 toJson。移交：DatasetStore 侧同型 API 无法报告截断（白名单外） | P1 | FIXED(实验侧)/KEPT(dataset侧移交) | 874eb6c0a2 + 1d05e18279 |
| ⑩ | `splitManifestsForVersion` 静默跳过 | dataset_store_splits.cpp:132；损坏/缺失 split 行静默跳过 | 已修：D7 决策=文件内最小修复——跳过计数 + 稳定令牌 qWarning `dataset.split_manifest_corrupt_skipped`（versionId+可数行数）。移交：Result 化需跨 3 个白名单外调用方（cli/foundry/data_platform_tools） | P2 | FIXED(最小)/KEPT(Result化移交) | 08c552f6a2 |
| ⑪ | `runById`/`promotionById` 损坏行 fail-open | store.cpp:996/:2113 + `loadRunLocked` :549 解析失败返 nullopt（与"不存在"不可区分） | 已修：D3 决策=typed 读取器 runRecordById/promotionRecordById（not_found/corrupt 分码）+ 判定路径全迁移（promotion 门 typed 拒绝、repeat 分类器区分 unreadable/vanished、debugger kCodeRunCorrupt、matrix 聚合拒绝）；旧 optional 签名保留为展示接缝。移交：src/cli 展示路径 corrupt→not-found 归并显示 | P1 | FIXED(判定路径)/KEPT(cli展示移交) | 874eb6c0a2 + 7f62b2b0c9 |
| ⑫ | capsule 生产 run 端到端重放 | src/experiment/capsule/（document/io/readiness/diff/portability）；r2 起挂起，需真实平台 | 已修(fixture级)：build→exportCapsule→loadCapsule→diff Identical→同内容重建 Identical→reloaded doc 判 Exact（随文件走）。移交：真实生产 run 上的执行面重放（需平台） | P3 | FIXED(fixture)/KEPT(真实平台移交) | e77443ddbc |

优先级依据（用户可感知性 × 触发概率）：P1 = 静默数据错误/判定失真路径（①②④⑤⑨⑪）；P2 = 边界条件、需特定输入形态（③⑥⑦⑧⑩）；P3 = 依赖真实平台的验证缺口（⑫）。

## 处置记录（随提交回填）

- 全部 12 条处置已于本轨完成：11 修 + ⑫ fixture 级收窄（真实平台部分书面保留）。
- 5 项移交清单见 DECISIONS.md 末节（dataset 侧 lineage API、splits Result 化、CLI 展示路径、真实平台重放、incomingEdges 同型下推）。
- 评审 P1-1（analyzeStudy 损坏行漏网）与 P2-1（limit≥10000 探测失效）已作为修复批次补入（5925e8aed9）。
