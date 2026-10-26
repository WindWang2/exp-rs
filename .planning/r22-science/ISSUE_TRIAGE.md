# R22-01 science Issue 分级（2026-10-25）

| Issue | 严重度 | 分级 | 证据 |
|---|---|---|---|
| #1707 distanceToForeground 原地覆盖 f | P1 | **OPEN_UNFIXED** | master `distance_transform.cpp:49-56` 求值循环 `f[q]=d*d+f[v[j]]`，q 递增时 v[j]<q 的 f[v[j]] 已被前序迭代改写；issue 附 3×4 复例 (0,3) 期望 8 得 6；本轨独立复算确认（见 ORACLES.md） |
| #1684 computeStatsFromAccumulators float 均值方差 | P1 | **OPEN_UNFIXED** | master `math_utils.cpp:156,160`；#1676 head 同函数同样实现（排重确认未修） |
| #1685 seed 路径 std::shuffle/distribution 不可移植 | P1 | **OPEN_UNFIXED** | 8 个调用点源码确认（split×3、utils.h、training_data、multires、kmeans reservoir、endmember×2 同步对、feature_matcher RANSAC）；faultlab/deterministic.h L5-9 佐证不可移植性 |
| #1686 Resampler 像元中心边界丢半像元 | P2 | **OPEN_UNFIXED** | master `resampler.cpp:35`（sampleNearest）与 `:121`（interpolate）均为 `u>width-1` 像元中心判定 |
| #1687 science 技术债（normalizedDifference -999 魔数、validCount 语义文档） | P3 | **部分处理** | -999 魔数在 #1676 中重构为 bothNormalizable 但语义未变（仍魔数）；本轨做文档化部分，nodata_utils 接线涉及更广调用方，记 CROSS_TRACK_FINDINGS |
| #1706 FeatherAccumulator 共享缓冲 | P1 | OUT_OF_SCOPE（本轮） | 文件属 src/analysis 之外？（待查：若在 src/operators/rs 则在范围内；容量原因留待下一里程碑，先记 TASKS.md） |

## #1706 复核（2026-10-25 追加）
`FeatherAccumulator` 位于 `src/operators/rs/rs_cartographic_operators.cpp`（在允许范围内）。
本轨 A1-A3 优先；#1706 列为 A4 候选，若时间/容量允许则实施，否则明确 NOT_DONE 移交。
