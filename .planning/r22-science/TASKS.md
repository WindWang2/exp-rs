# R22-01 science TASKS

## M1: 基础设施（进行中）
- [x] 盘点 master/PR/issue（BASELINE.md）
- [x] #1676 排重（源码级）
- [x] worktree + 分支
- [ ] build-r22-science 配置完成
- [ ] 基线测试族跑绿（primitives5 / math_utils / resampler / rs_operators / kmeans / obia / split）

## M2: A1 数值正确性（#1707 / #1684）
- [ ] RED 测试：test_r22_science_distance（3×4 复例 + 随机暴力对拍 >3000）
- [ ] 修复 squaredDistancePass 独立输出缓冲
- [ ] GREEN + 双跑
- [ ] RED 测试：test_r22_science_stats（10000.3±0.5、常量、N=1、大偏移小方差）
- [ ] 修复 computeStatsFromAccumulators double 均值
- [ ] GREEN + 双跑
- [ ] 提交 fix(r22-science): …

## M3: A2 确定性随机（#1685）
- [ ] 新增 src/processing/algorithms/portable_random.h（Qt-free：mt19937 + 固定 Fisher-Yates/有界整数/Box-Muller）
- [ ] golden 序列测试（硬编码期望值）
- [ ] 替换 8 组调用点（split×3、utils.h、training、multires、kmeans、endmember×2、RANSAC）
- [ ] 受影响测试族全绿
- [ ] 提交 fix(r22-science): …

## M4: A3 重采样边界（#1686）
- [ ] RED 测试：test_r22_science_resampler（4×4→8×8 边缘、亚像元平移、旋转、足迹外 NoData）
- [ ] 修复 sampleNearest/interpolate 足迹判定 → [-0.5, w-0.5]
- [ ] GREEN + 双跑
- [ ] 提交 fix(r22-science): …

## M5: 评审与交付
- [ ] R1 独立 review（subagent）→ REVIEW_R1.md → 修复闭环
- [ ] R2 独立 review（另一 subagent）→ REVIEW_R2.md → 修复闭环
- [ ] TEST_RESULTS.md / PERF_BASELINE.md(N/A 或基线) / CROSS_TRACK_FINDINGS.md / FINAL_REPORT.md / PR_BODY.md
- [ ] Push + PR

## 备选（容量允许才做）
- #1706 FeatherAccumulator（P1，在允许范围，若 M2-M4 顺利）
- #1687 文档化部分
