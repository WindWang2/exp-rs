# R22-03 PERF BASELINE

## 本 PR（#1706 修复）性能影响

- 无吞吐/延迟基准需求（正确性修复优先）；未做正式基准，标记 **N/A（无性能回归证据要求）**。
- 定性分析（R1/R2 复核一致）：
  - `slideUp` 由 (slots+2) 个 Mat 变为 (2·slots+1) 个同几何 Mat 的 memcpy——O(window)/tile-row，与一次 add() 同量级，immaterial。
  - derived 内层循环 per-class `ptr<float>(r)` 未 hoist——每次多几次指针加法，相对 GDAL 写出与模型前向为噪声级（R1 P3，不落实以避免微扰语义关键路径）。
  - **内存**：窗口内每 slot 增加一份权重账本：Δ = (slots−1) × span × rasterW × 4 B。有界于 (tile+2·halo) 行窗口，非全栅格。极端算例（R1 给出）：C=21/tile512/halo128/rasterW25000 ≈ 3.3GB（旧 1.76GB）。follow-up 候选：半精度账本（未实现，见 FINAL_REPORT 展望）。

## 后续里程碑（C5）基准框架

按任务书 C5（确定性与吞吐）将在后续工作包执行：C=2/3/5、多 tile、CPU 优先；GPU 项本机不可用（NOT_EXECUTED）。本 PR 不预支。
