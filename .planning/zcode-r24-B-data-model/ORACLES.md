# R24-B ORACLES — 客观验收

- **O-B0** `DATA_INFERENCE_MAP.md` 基于实读 file:line 链路（产品导入→reader/COG/STAC→dataset→preflight→session→tile inference→原子发布→lineage→experiment/bundle），标注缓存/事务/生命周期；无空架构图。
- **O-B1** ≥3 个跨模块 E2E 实跑（exit code + 内容 digest + 输入配置）；1 条故障注入→回滚示例。E2E：①导入+身份+缓存；②samples→split/audit→tiny 模型推理→label/置信；③取消→原子发布回滚→replay→指标可重复。
- **O-B2** 非法 URL/坏文件/超维度/无权限/cancel/OOM/版本错 typed fail-closed 测试全绿；已合并安全修复（SSRF/RangeDiskBlockStore/原子发布）不被回退。
- **O-B3** metamorphic ≥3 类（tile size / tile 顺序 / provider fallback），量化一致性阈值；空间泄漏反例可被检出。
- **O-B4** 性能 before/after 各 5 次，CPU/RSS/磁盘账目完整；无可信度量不声称加速；资源不足场景标 NOT_EXECUTED。
- **O-B5** 两轮独立 reviewer（不同 fresh 上下文）P0-P2 闭合；`scripts/check_merge_registration_integrity.py --require-refs` 通过；worktree 独立；非 draft PR URL 真实可验证。

证据落点：`TEST_RESULTS.md`、`PERF_BASELINE.md`、`REVIEW_R1.md`、`REVIEW_R2.md`、`FINAL_REPORT.md`。
