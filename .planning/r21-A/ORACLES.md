# ORACLES — r21-A

## 硬性 Oracle（任务 §7）

- O-A1 R20 #1671 七族既有回归两遍绿；负向数值输入不 silent success。
- O-A2 每个改动算法：可人工核算 tiny raster/spectrum oracle + 物理容差 + 跨 block size 输出一致；顺序可重复；未定义例外 typed diagnostic。
- O-A3 NoData/NaN/±Inf/无效 geotransform/极小极大栅格/空 ROI/时间重复乱序/SAR dB-linear/极端浮点覆盖。
- O-A4 同条件基线 vs HEAD：吞吐/RSS/科学误差记录；>10% 退化必须优化或证明可接受并列风险。
- O-A5 3 条完整工作流实跑（fixture 可复现）。
- O-G1 新鲜盘点（每次提交前 re-fetch）。
- O-G2 RED/GREEN 两遍 + 边界反例 + 生产路径证明。
- O-G3 性能基准同条件、内存上界。
- O-G4 受影响测试两遍绿 + 两个独立 reviewer。
- O-G5 残余 skip/NOT_EXECUTED 逐项诚实归因。
- O-G6 PR 文件 100% 白名单内；merged patch 不静默回滚。
- O-G7 无 TODO 假实现/占位 mock/假数据 PASS。

## 通用纪律

- 不能在未运行测试情况下宣称 PASS；NOT_EXECUTED 必须显式标注。
- 每个 RED 都要给出失败输出摘录（RED_GREEN.md）。
