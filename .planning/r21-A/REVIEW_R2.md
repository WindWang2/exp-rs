# REVIEW_R2 — r21-A

评审代理：fresh general-purpose reviewer（独立于 R1 与实现者；对抗性边界 + 变异审计 + 独立重推导）。
输入：`git diff 911e0b17...HEAD` + .planning/r21-A 文档；未修改 worktree（变异在 /home/kevin/project/mut-scratch）。
评审者复跑/重链 23+ 个测试二进制；对 27 个生产变异做"测试是否抓住"审计（16 抓住 / 11 存活）。

## 结论

- P0：无。P1：1（R2-1，已修）。P2：2（R2-2、R2-3，已修/已处置）。P3：9（7 修、2 记录）。
- R2 独立重推导两个 oracle 与实现一致；R1-1 修复经变异 M1 证实有效（移除目标端重置 → 新测试失败）。

## R2-1（P1，已修）test_object_hierarchy 钉旧 paint 契约；陈旧二进制掩盖

- 位置：tests/test_object_hierarchy.cpp:486-497
- 问题：commit 33eda0e 把 paint 契约改为"0=未分类合法、负数拒绝"，该测试仍 REQUIRE_FALSE(ok) 且查 ">= 1"。
  二进制 build-r21-A/tests/test_object_hierarchy 早于 rs_class_raster.cpp 改动未被重链 → 我此前的家族
  "两遍绿"结论对该二进制不成立（R2 重链后暴露 1 failed）。
- 修复：改为新契约——id 0 必须成功且写 NoData（读回 labelAt==0、其余段 class 3 正常）；负数必须拒绝且
  消息含 ">= 0"（旧用例覆盖 0，负数拒绝此前无任何测试）。
- 验证：重链后 271/36 全绿。
- 附带教训（已固化）：本机增量构建会因 mtime 未变保留陈旧目标；凡源码改动后必须重链目标再跑。

## R2-2（P2，已修）ramp 可靠性探针被单个 NaN 污染 → 真实数据几乎必 stamped "low"

- 位置：src/operators/rs/rs_sar_interferogram_operator.cpp:232-238
- 问题：fitter.addSample 跳过 NaN，但探针无条件累加 phasor；一个 NaN → |sum|/N = NaN → NaN>=0.15 false →
  误报 "low" + warning "resultant length nan" + 元数据盖章。生产 SLC 对的核配准 void/layover 零点即触发。
- 修复：仅在 isfinite(phi) 时累加与计数（与 fitter 同一有效性规则）。

## R2-3（P2，已处置）变异审计：11/27 存活；RED_GREEN 的 "GREEN×2" 混用家族回归与定向测试

- 存活变异及本轮补的定向测试：
  | 存活变异 | 补的测试 |
  |---|---|
  | M23 kmeans scale 读回 fit 后 | test_rs_operators "scale=false clusters on raw DN"（差分 oracle：两波段动态范围差 30×，raw 与 z-score 划分必须不同；忽略 flag 则两运行逐字节相同 → 失败） |
  | M25 恒定像素 z=0 | test_temporal_operators_10 "constant pixel's CUSUM at zero"（drift=0.5×4 景：修复后 S=0/maxAbs=0，bug 给 −2/2；变化像素作对照） |
  | M20 histogramMatch 总体标准差 | test_image_fusion 小 tile oracle（4 样本：样本尺度 11.547 落在参考极值 10/40；总体尺度给 13.66/36.34） |
  | M3 dNBR 丢 −999 哨兵规则 | test_image_fusion bothNormalizable 谓词逐条钉死（哨兵/阈值/等负/NaN/Inf/合法 0） |
  | M2 operator airmass 门未按方法限定 | R1-3 控制测试已覆盖内核门；operator 门经 sar/atmospheric 家族 + R1-3 注释说明（该门在 operator 层，测试经内核门间接保护；接受） |
  | M13/M22/M24/M26/M27/M30 | 分别记录：cache-pollution guard（perf）、RF row-0（stock OpenCV 不可达）、锁（无 TSan）、算子接线（R1-7 已记）、obia 全簇映射（测试数据每簇都有训练段）、Rayleigh（无生产调用方、6S LUT 不变量测试间接覆盖） |
- 文档修正：RED_GREEN.md 增列"定向测试 vs 家族回归"列；TEST_RESULTS.md 计数刷新为实测值。

## P3（处置）

1. WindowMean 下沿重复时间戳被截断（obs {0,0,0,10} t=10 r=10 → 26.5/2 而非 14/4）——**已修**：
   start 回退到等值 run 的首元素；回归测试钉死（含 r=0 重复 at t → 3.0/2）。
2. sar_foundation5 oracle 注释 cos 值 0.8704 → 0.8480（实测）——**已修**。
3. rs_sar_terrain_masks_operator.cpp:258 注释缩进——**已修**。
4. DEM 单元映射 4 份重复 + 不 trim——**已修**：拆出 demUnitScaleFromName（唯一所有者），
   demUnitScaleToMetres 委托之；flatten/correction 两个内联副本收敛到该 helper；helper 增加 trim。
5. 校准门对不披露 classOrder 的后端静默降级——**已修**：applyCalibration && backendOrder 空时
   SICNU_LOG_WARN 明示只做列数检查。
6. TEST_RESULTS 计数陈旧（atmospheric 37540→37548/48；obia 863→871/16 等）——**已刷新为实测**。
7. 并发测试注释过度声称覆盖 lazy 路径——**已改为准确描述**（30 段全走 full-index；lazy 由
   copy/move 与 unknown-id 用例覆盖）。
8. pixelCount 仍返回 int（两个生产调用方用 int API）——**记录不修**（预存在、仅 >2^31/段才溢出）。
9. 新拒绝路径的 staged 清理隐式——**已加注释**（StagedRasterOutput 析构负责，R2 已验证析构顺序）。

## R2 明确验证正确的要点（摘录）

- window_mean oracle 独立重算：start=0/endExcl=5/5 obs/13/5=2.6/filled=false。
- sin15/sin35=0.4512372348 与生产 gamma=v·sinθi/sinθ0 一致。
- R1-1 完整（变异 M1 使新测试失败，症状正是 stale cache 1000 vs 100）；28 项 RsSegmentMap 边界探针全过
  （全零标签、单段、1×1、8 vs 9 段阈值切换、copy 后查询、双向 move-assign、100 个未知 id 风暴）。
- DEM 单元接线顺序正确（normalizeHalo→缩放→Horn 梯度→几何核）于全部三个算子。
- terrainRadiometricFactor 域（0/90/>90/负/NaN 任一侧）全 NaN、平坦=1、sin 形式 1e-12。
- DOS2 控制 oracle 经 gdb 实测：darkLevel=0.12、T=e^−0.2=0.81873、(0.4−0.12+0.01)/T=0.35421 与测试一致；
  大有限 airmass（1e30）仍被内核门捕获（T 下溢为 0）。
- OLS NaN 丰度/误差对调用方 QA 一致（operator 跳过 NaN）。
- continuumRemoval 哨兵（−9999）不与合法比值碰撞（有效输出恒在 [0,1]）。
- pcaFusion/PCA/MNF 边界（单像素、2 像素 n−1=1、全 NoData 带、不相交 NoData、±Inf、常数带）无 NaN 泄漏/除零/崩溃。
- dNBR 融合语义保持（谓词逐字节相同 + safeDiv 零分母 NaN）。
- 无 TODO/FIXME/占位；新拒绝路径无资源泄漏。
