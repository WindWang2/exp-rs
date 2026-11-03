# R27-T05 Bench Baseline

本轨为正确性/安全修复轨，无性能优化承诺；未做性能声称。构建事实记录：

- configure（含 tests）：~6 分钟（flock 串行下）。
- Release lane（test_experiment_evaluation / test_placeholder_grammar / test_repair_planner_completion 及其依赖）：进行中，object 数 2040+（含 qgis_gui 尾巴）。
- 资源峰值：构建期本轨 ≤4 编译进程（-j2 限制下实际 2-4），avail RAM ≥43GB，无 swap 增长，无 OOM。
- 轻轨 grader lane（4 target）：~7 分钟全编链+测试通过。

运行时性能变化（本轨改动均为 O(1)/O(n) 常数级：归一化单遍 UTF-8 折叠、
digest 复核单读单哈希、validation 增量集合操作）——无测量前不做提升声称。
