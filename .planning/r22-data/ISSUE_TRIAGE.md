# R22-02 (r22-data) ISSUE_TRIAGE — 第一批评级（2026-10-25，源码验真后）

| Issue | 标题（摘要） | 评级 | 依据 |
|---|---|---|---|
| #1679 | isPrivateHost 未以 ?/# 终止 authority → SSRF 绕过 | **OPEN_UNFIXED / P0 / 立即修** | BASELINE 表内源码证据；issue 报告的 `find('@')`+firstSlash 逻辑与 master 完全一致 |
| #1700 | RangeDiskBlockStore 块键不含 blockSize → 静默错位字节 | **OPEN_UNFIXED / P1 / 立即修** | identityBasis 无 blockSize；updateConfig 只 dropAllLocked |
| #1701 | atomic_fs 提交标记 rename 被忽略/journal 未校验/陈旧 .bak | **OPEN_UNFIXED / P2 / 修** | 两处 rename 返回值忽略 + writeJournalFile 无 good() 检查 + journal 写前不清 .bak |
| #1702 | SamplePromoter 忽略 addSourceAssetsToDraft 失败 | **OPEN_UNFIXED / P2 / 修** | 4 处丢弃 Result |
| #1680 | ArtifactObjectPool 并发 put 同 digest 误报失败 | **OPEN_UNFIXED / P2 / 修** | rename 失败分支无 exists+digest 复查；tmp 名仅 pid |
| #1703 | data 线清理清单（3 项） | **OPEN_UNFIXED / P3 / 修 ①③，② 按证据处置** | ① int 溢出；② readMask 无外部调用者→与 dead-code 处置一并评估（保守：仅修语义不动公共 API，若无调用者记录 CROSS_TRACK 待 GUI 轨确认后删）；③ stepRow 误报 |

## 未进入本轮实施的同域候选（诚实记录，不冒充完成）

- B3（catalog/栅格矢量导入全链路）、B5（10 万级性能）、B6（系统接口真值）为后续里程碑方向；本轮先闭环 P0/P1/P2+P3 债务（对应任务书 B0/B1/B2/B4 核心），B3/B5/B6 的可验证增量视首轮余量决定，未做则明确 NOT_STARTED 入 FINAL_REPORT。
- #1587（mirrorAllAssets 丢弃治理库写入失败）：data 域真实缺陷，若首轮余量允许纳入 B4 补充；否则记入 CROSS_TRACK_FINDINGS/后续。
- #1548（身份解析冷路径自死锁）、#1545（range cache 探针不传播 SSRF 标志）：需运行时复现窗口，列为下一迭代候选。

## 排重

- 5 个 open PR（#1673–#1677）文件清单逐条比对，无一处触碰本轨 6 个 issue 的目标文件（除 dataset_store.cpp 邻域，见矩阵）→ 无 IN_PROGRESS_PR。
- master log 检索 `isPrivateHost|identityBasis|publish-journal|addSourceAssetsToDraft|puttmp` 无 2026-10-23 之后的修复提交 → 无 FIXED_OPEN。
