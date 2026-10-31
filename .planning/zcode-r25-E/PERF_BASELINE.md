# PERF_BASELINE — R25-E

全部改动为正确性/可取消性/可解释性，无可声称的性能收益：

| 改动 | 性能影响 | 结论 |
|---|---|---|
| discovery 取消检查（3 循环各一次 relaxed atomic load/条目） | ~ns 级/条目，相对每条目至少一次 stat 的目录遍历不可测 | NO_MEASURABLE_BENEFIT；行为语义有测试 |
| CT-C6 seam：GUI 批处理目录扫描 2 次→1 次 | 网络盘场景减半目录遍历（r24-c CT-C6 的目标）；本地 SSD 不可测 | 有明确方向性收益，不做墙钟断言（#1699 纪律），由行为测试锁定"单次扫描"语义 |
| grading_evidence/run_integrity JSON 块 | 每包/摘要 +3~5 个小成员的序列化 | NO_MEASURABLE_BENEFIT |
| 空白作答 isBlankAnswer 扫描 | O(len) 一次字符串扫描/作答 | NO_MEASURABLE_BENEFIT |
| run_lab.cmd 一行改动 | N/A | N/A |
未做任何 wall-clock 硬门槛；测试无时间断言（mid-walk 测试只断言结果语义）。
