# R4 DECISIONS — 契约决策记录（定稿）

每条涉及语义契约选择的处置在此记录：决策、备选、理由、影响面。全部决策遵循"零新功能方向"铁律——只把既有合同的静默失败面显式化。

## D1 benchmark 重复 truth/prediction 行 → 拒绝（typed 诊断）
- 现状：`benchmark_runner.cpp:307` `truthById.insert` 对重复 sampleId last-wins；:348 预测循环对每条重复 prediction 各 `matrix.increment` 一次 → 混淆矩阵与全部派生指标被静默重加权。
- 备选：①拒绝 ②去重 ③显式加权 API。
- **决策：①拒绝**。truths 重复 sampleId 或 predictions 重复 sampleId → typed 失败 `experiment.benchmark_duplicate_sample`，消息可数（列出首个重复 id 与两侧行数）。
- 理由：去重任意挑选一行同样伪造证据；显式加权是新增 API（本轨禁新功能方向）；拒绝与同文件既有合同一致（`benchmark_empty_inputs` / `benchmark_no_overlap` 都是 typed 拒绝先例）。
- 影响面：只影响此前静默产出错误指标的输入；合法输入零行为变化。

## D2 `runsForCell` 超限 → typed 拒绝
- 现状：`experiment_matrix.cpp:304` SQL LIMIT=kMaxMatrixCells，超限静默截断；头注（h:104-105）自认"statistics over a truncated list would skew toward the earliest runs with no marker anywhere in the report"。
- 备选：①typed 拒绝 ②显式 truncation 标记字段。
- **决策：①typed 拒绝**。`runsForCell` 改返 `Result<QStringList>`，诊断码 `experiment.matrix_cell_runs_overflow`；实现查 limit+1 行检测溢出，消息含实际下界（>1000 可数）。
- 理由：头注既有合同原文是 "refusal is typed, not a silent truncation"（h:30-31，对 enumerateCells）；同一模块同一 honesty 契约对 runs 轴同样成立。标记方案会把"统计悄悄算在子集上"留给下游决定是否理会——这正是合同要堵的静默面。
- 影响面：调用方全在白名单内（experiment_matrix.cpp:322/:359、study_analysis.cpp:141、study_export.cpp:547），逐一传播/记账；cross-session 重跑 >1000 次的 cell 从"静默错统计"变为"typed 拒绝"，用户可感知（诚实性提升）。

## D3 `runById`/`promotionById` 损坏行 → typed 新读取器 + 白名单内判定路径迁移，旧签名保留
- 现状：`store.cpp:549 loadRunLocked` / `:2113 promotionById` 解析失败返 nullopt，与"不存在"不可区分；同文件已有修复先例（`ExistingRun` tri-state 写路径、`promotionsForModel` fail-closed 读路径）。
- 约束：`runById` 有白名单外调用方（src/cli/cli_dataset_commands.cpp:555/556/593/656），`promotionById` 有白名单外测试调用方（tests/test_mlops9_evidence.cpp:449）——签名不可改。
- **决策**：新增 typed 读取器 `runRecordById(id) → Result<ExperimentRun>`、`promotionRecordById(id) → Result<PromotionRecord>`，诊断码区分 `experiment.run_not_found` / `experiment.run_corrupt`（promotion 同构）；白名单内全部判定路径迁移（promotion.cpp evaluate/record、repeat_execution.cpp 分类器、debugger/evidence_source.cpp、experiment_matrix.cpp 聚合）；`runById`/`promotionById` 变为 typed 读取器上的薄包装（corrupt→nullopt），头注写明"仅展示路径；判定路径必须用 typed 读取器"。
- 理由：fail-open 的实际危害在"判定语义把损坏当缺失"（promotion gate、verdict、聚合）；CLI 展示路径 nullopt → "no recorded run" 报错本身即拒绝操作，无静默错误。这样在白名单内达成 fail-closed 合同，不越界改 src/cli。
- 遗留记账：CLI 路径的 corrupt→not-found 归并显示移交后续 track（无判定危害）。

## D4 lineage 截断 API 化 → 结果结构携带 truncated/total
- 现状：`experiment_store.h:177 allLineageEdges( limit = 100000 )` 纯 QVector 返回，超限静默；`lineage.h` 的 `LineageQueryResult` 已有 `budgetExhausted` 先例。
- 备选：①返回结构 {edges, truncated, total} ②游标参数化。
- **决策：①**。`LineageEdgePage { QVector<LineageEdge> edges; bool truncated; qint64 total; }`（total 由 COUNT(*) 或截断探测给出）；`allLineageEdges` 保留为兼容包装（取 .edges），`LineageGraph` 组装处（lineage.cpp:130/132）改用分页读取器并把截断事实写入 `LineageQueryResult`（新增 `sourceTruncated` 标记进 toJson）。
- 理由：与模块内 `budgetExhausted` 先例同构；游标化是 API 重设计（超范围）。
- 影响面：调用方仅 lineage.cpp（白名单内）+ DatasetStore 侧同型 API 不动（白名单外，移交记账）。

## D5 旧 bundle 十进制 seed 超安全范围 → typed 诊断，不猜值
- 现状：`reproduction_bundle_import.cpp:211-216` 无 `seed_hex` 时 `toDouble(0)` 强转 quint64；>2^53 失真、≥2^63 得到实现定义值。
- **决策**：无 `seed_hex` 且十进制 |seed| 不在 [0, 2^53] 精确双精度区间时 → import 失败（typed `experiment.bundle_seed_not_lossless`），消息指引"该 bundle 早于 seed_hex pin，seed 无法无损恢复"。0 ≤ seed < 2^53 的旧 bundle 保持原路径（真无损）。
- 理由：#1326 已确立 seed_hex 为无损通道；对不能无损恢复的值"猜一个"比拒绝更危害复现语义（false reproducibility）。negative JSON 数（r2 记录的"负 JSON 数"形态）同落入拒绝面。
- 影响面：仅 legacy bundle（无 seed_hex）且 seed 超界；#1326 后的 bundle 全部带 seed_hex，零影响。

## D6 markdown ≥4 反引号残余向量 → 围栏长度自适应（统一导出层）
- 现状：`lab_report_writers.cpp` 6 处 `"````json\n%1\n````"` 硬编码四反引号；载荷（compactJson 单行）内含 ≥4 连续反引号即提前闭合围栏。
- **决策**：新增统一助手 `jsonCodeFence(value)`：计算载荷内最长反引号串 L，围栏长度 = max(4, L+1)（CommonMark 规则），6 处调用点全部改走助手。载荷字节不变。
- 理由：CommonMark 认可的最小修复；归一在导出层单点（对齐 WP-D 审查门禁"禁止每个字段各修各的"）。

## D7 ⑩ `splitManifestsForVersion` 静默跳过 → 跳过显式化（文件内最小修复）
- 现状：`dataset_store_splits.cpp:155-163` corrupt 行 `if (parsed)` 静默跳过；同文件 `splitManifestById` 注释自称 fail-conservative。
- 约束：3 个调用方全在白名单外（src/cli:422、src/dataset/foundry_service.cpp:86、src/agent/data_platform_tools.cpp:391）→ 签名不可改，改动仅限 dataset_store_splits.cpp。
- **决策**：跳过计数 + `qWarning`(qInstallMessageHandler 可观测) 带稳定诊断文本 `dataset.split_manifest_corrupt_skipped`（含 versionId 与可数计数）；头注写明跳过语义与消费方风险。
- 理由：在白名单内能达成的最大显式化；改签名必越界。移交：Result 化需跨 3 个白名单外文件，归后续 track。

## D8 ⑥ 50-twin 上限 → verdict 显式 capped 标记
- 现状：`repeat_execution.cpp:136` `/*limit=*/50` 硬编码，超限无感。
- **决策**：`kMaxTwinScan = 50` 常量化；`Verdict` 增 `bool twinScanCapped` 字段（默认 false）+ `toJson` 写 `twin_scan_capped`；当返回 twins 数 == kMaxTwinScan 时置位并在 `reasons` 追加可数说明。判定逻辑不变（50 内 twins 语义照旧）。
- 理由：typed 诊断字段是既有 Verdict 结构的缺失契约条目（防御性），非新功能。

## D2b buildStudyReport 的拒绝形态（④ 传播中的签名约束）
- 约束：`reportFromStore`（studio_live）的调用方位于 `src/app/experiment_studio_dock.cpp` 与
  `tests/test_studio_live_e2e.cpp`——均在本轨白名单外，buildStudyReport 改 Result 签名会强制越界修改。
- 决策：buildStudyReport 保持 `StudyReport` 返回；analysis 拒绝时返回 STOPPED 报告——
  stoppedReason 携带 typed 诊断码+消息，runTable/curves/envelopes 全空、四项计数全零。
  空表+零计数+显式原因 = 拒绝可见且零伪造（不造任何行去填充报告形状）。
- typed 拒绝属于 analyzeStudy/aggregate（它们的全部门内调用方已迁移）。

## 移交清单（本轨书面保留项汇总）
1. DatasetStore::allLineageEdges 截断不可报告（D4；src/dataset 白名单外）。
2. splitManifestsForVersion 的 Result 化（D7；3 个白名单外调用方）。
3. src/cli 的 runById/promotionById 展示路径 corrupt→not-found 归并显示（D3；无判定危害）。
4. ⑫ 真实生产 run 上的执行面端到端重放（需真实平台，r2 起延续）。
5. incomingEdges 与 outgoingEdges 同型的 edge-kind 下推（当前无消费方受影响，未做零漂移改动）。
