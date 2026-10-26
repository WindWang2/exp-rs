# R22-01 science PERF_BASELINE

## 结论：N/A（本轮无性能主张）

本轮四项修复均为正确性修复，未做性能优化，不宣称任何性能变化。
按任务书 §A 各交付门："有可量化性能诉求时附前后基准" —— 本轮无该诉求。

## 结构性开销说明（非基准，供评审参考）
- #1707：每行/列 pass 增加一个输出缓冲（各 width/height 个 double），在
  distanceToForeground 既有 scratch（f, z×2, row, col）上增加 rowOut/colOut 两个
  O(n) 向量；每 pass 数据移动量不变（读 f 写 out，原本就是读写各一次）。
- #1684：computeStatsFromAccumulators 无循环结构变化（同为 O(1) 公式）；
  rs_segment_features 每像元每波段增加 4 次 double 加减（Kahan），热循环内
  无分配。
- #1685：与 std::shuffle/uniform_int/normal 同阶；Box-Muller 每值 2 draw + 1 log
  + 1 sqrt + 1 cos（std::normal_distribution 典型实现同为 Ziggurat/Box-Muller
  级别）。
- #1686：判定常数从 width-1 改 width-0.5，零额外计算。
