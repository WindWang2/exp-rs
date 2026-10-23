# hardening(r20-01): 遥感科学算法数值正确性收敛 — 趋势时间戳配对 / viewshed NoData 契约 / 镶嵌 X 翻转防线 / 分类器契约 / SAR 角域与 InSAR 条件数

<!-- 以下字段由最终提交前填充实测值 -->

## 基线与范围

- BASE SHA: `28e1891390bb812dc7ef2fe045ac90492c9525f1`（origin/master @ 2026-10-22）
- HEAD SHA: `1f0a305eb9475712a669d9e7be6e4c6baf896082`（10 commit：6 fix + 1 test/chore + 2 fix + docs）
- 分支：`agent/r20-01-scientific-algorithms-20261022T160501Z`
- 五轨分工之 01/05（科学算法/处理链/数值·性能）。源码仅触碰 `src/analysis/classification/**`、`src/processing/algorithms/**` 与 1 个算法直绑 operator `src/operators/rs/rs_sar_backscatter_operator.cpp`（R1-P3 缝校验）；测试全部追加于 7 个既有注册测试文件（零 CMake 变更）。

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

## 证据（本地实测，全部命令与退出码见 `.planning/zcode-r20-01/TEST_RESULTS.md`）

- 构建：全局锁 `exp-rs-zcode-global-build.lock`、`-j2`（一次 ld 段错误为工具链瞬态，按协议降 `-j1` 重试恢复）：基线全量 exit 0 → 修复增量 exit 0 → RED 构建 exit 0 → 恢复构建 exit 0 → R2 处置构建 exit 0
- **RED（master 源码 + 新测试，`git checkout origin/master -- src/` 后重建直跑）**：6/6 二进制 exit=42，每个**恰好 1 个新用例失败、既有用例全过**：
  - 趋势：`0.1f == Approx(0.0667)`（buggy 前缀配对实测值 vs 解析真值）
  - SAR：`REQUIRE(isnan) → false`，message `theta=90 gamma0=1.63312e+16`（预测毒值实测穿透）
  - viewshed/MLP/ISODATA/InSAR 同样精准失败（详见 TEST_RESULTS.md）
  - mosaic：对 master 头文件编译失败（xDirectionMismatch 字段不存在）= 编译级 RED
- **GREEN（HEAD）×2 + R2 处置后再两遍**（断言数逐遍一致）：9 目标全绿 exit=0 —— 84/4, 1453/7, 101/10, 21/4, 461/8, 284/11, 113/13, 910/29, 404/16
- 回归：R1 评审员同机实跑 21 个相关目标全绿（含 test_quality_mosaic_operator 2002、test_mosaic_blend 5072、test_classification_pipeline 240、test_classifier_load_save 623、test_terrain_analytics_e2e 1088 等）

## 跨轨风险

- F-02 修复兑现 viewshedR3 头文件既有承诺；消费方 `src/agent/spatial_tools/terrain_spatial_tools.cpp` 按既有 `v==255` 过滤逻辑自动恢复正确（未修改该文件）。
- F-03 使 X 翻转输入从"静默镜像"变为硬错误（与 Y 镜像同纪律）；正常 north-up 输入无行为变化。
- 其余修复均在算法内核内部，无 schema/语义变更。
- 跨轨登记见 `.planning/zcode-r20-01/CROSS_TRACK_FINDINGS.md`（#1541 经复核已修未关；#1642/#1646 其余项归 02/03 轨）。

## Known Fail / Not Executed

- 无测试失败、无 SKIP。OpenCV 5.0.0 ANN_MLP NaN-model 拒绝路径：F-04 测试按既有约定显式 SUCCEED 分支处理（本次实跑走了真实训练路径，8 断言全过）。
- follow-up（非阻塞）：R2-2 其余 CV 后端 type 校验统一、R2-5 raster 路径元数据角、R1-5 dense_linalg 相对主元阈值、#1591 RsSegmentMap 潜在竞态提案、fast_6s 水汽 0.5 系数物理横对照。

## 独立评审（两轮 fresh reviewer，REVIEW_R1.md / REVIEW_R2.md 全文入库）

- **R1**：7 项内核修复全部判定正确（F-07 代数逐项手推复核）；发现测试 P0（sar_kernels 限定名编译失败）+ P1（Float32 oracle 偏差）+ 2×P3——全部处置。
- **R2**：全量 diff 复审通过、无 P0/P1；operator 缝测试缺口 P2 + 3×P3（MLP 守卫/死代码/账本）——全部处置；R2-5（元数据缝）记 follow-up。
- 处置后 9 目标连续两遍全绿。无遗留 P0/P1/P2。

## 说明

不等待 CI；不自动合并。账本全部收录于 `.planning/zcode-r20-01/`。
