# hardening(r20-01): 遥感科学算法数值正确性收敛 — 趋势时间戳配对 / viewshed NoData 契约 / 镶嵌 X 翻转防线 / 分类器契约 / SAR 角域与 InSAR 条件数

<!-- 以下字段由最终提交前填充实测值 -->

## 基线与范围

- BASE SHA: `28e1891390bb812dc7ef2fe045ac90492c9525f1`（origin/master @ 2026-10-22）
- HEAD SHA: <待填>
- 分支：`agent/r20-01-scientific-algorithms-20261022T160501Z`
- 五轨分工之 01/05（科学算法/处理链/数值·性能），仅触碰 `src/analysis/**`、`src/processing/algorithms/**` 与 7 个既有注册测试文件（零 CMake 变更）。

## 修复清单（7 项，全部 master-RED → HEAD-GREEN×2）

| ID | 严重度 | 模块 | 缺陷摘要 |
|---|---|---|---|
| F-01 | **P1** | trend_analysis.cpp | 栅格批趋势分析紧凑化丢时间戳配对，序列中部缺帧时 Theil-Sen 斜率系统性错误（单序列入口正确、批入口错误——两入口不一致即铁证） |
| F-02 | P2 | terrain_viewshed.cpp | viewshedR3 从不写 nodataByte（头文件契约违约），agent 地形工具把 DEM-NoData 计入 visibleFraction 分母 |
| F-03 | P2 | mosaic_plan.cpp/.h | 镶嵌网格资格只防 Y 镜像不防 X 翻转（gt[1] 符号），东西翻转场景内容镜像进入镶嵌；对称补 xDirectionMismatch 诊断+fail-closed |
| F-04 | P2 | rs_classifier_mlp.cpp | 单类训练的填充哨兵使 classOrder 返回 [c,c] 违反严格递增契约，sidecar 写读自毁（predict-only 误报"sidecar 损坏"） |
| F-05 | P3 | rs_classifier_isodata.cpp | fit 缺 y 类型/形状校验（KMeans 已有同构守卫），类型双读+空 data 解引用路径 |
| F-06 | P2 | sar_calibration.cpp | 栅格入射角只查 isfinite 不查 (0°,90°) 物理域；θ=90° 产生 ~1.6e16 有限毒值穿过 finishValue v>0 门 |
| F-07 | P2 | sar_insar.cpp | 相位斜坡正规方程用原始像素坐标，20k 场景条件数 ≥1e17 超 double 分辨率；改为中心化/归一化拟合+系数回代，模型契约不变 |

## 证据（本地实测，构建/测试命令与退出码）

- 构建：全局锁 `exp-rs-zcode-global-build.lock`、`-j2`、Debug+ENABLE_TESTS=ON，退出码 <待填>
- RED（master 源码 + 新测试）：<待填：各测试失败断言与退出码>
- GREEN（HEAD）×2：<待填：两遍独立复跑全部通过>
- 回归：<待填：受影响模块既有测试族结果>

## 跨轨风险

- F-02 修复兑现 viewshedR3 头文件既有承诺；消费方 `src/agent/spatial_tools/terrain_spatial_tools.cpp` 按既有 `v==255` 过滤逻辑自动恢复正确（未修改该文件）。
- F-03 使 X 翻转输入从"静默镜像"变为硬错误（与 Y 镜像同纪律）；正常 north-up 输入无行为变化。
- 其余修复均在算法内核内部，无 schema/语义变更。
- 跨轨登记见 `.planning/zcode-r20-01/CROSS_TRACK_FINDINGS.md`（#1541 经复核已修未关；#1642/#1646 其余项归 02/03 轨）。

## Known Fail / Not Executed

- OpenCV 5.0.0 ANN_MLP NaN-model 拒绝路径：F-04 测试按既有约定显式 SUCCEED 分支处理。
- <待填：其余>

## 独立评审

- REVIEW_R1：<待填>
- REVIEW_R2：<待填>

## 说明

不等待 CI；不自动合并。账本全部收录于 `.planning/zcode-r20-01/`。
