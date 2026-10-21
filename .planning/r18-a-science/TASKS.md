# R18-A TASKS — 工作包分解（Recon → RED → 定位 → 极小修复 → GREEN×2 → 对照 → Review → PR）

阶段推进规则：单工作包一个可归因聚焦修改；达 Oracle 后进下一包。批次划分（任务书 A10）：
Batch-1 = 正确性 P1/P2（WP1-WP5）；Batch-2 = 契约完备（WP6-WP8）；Batch-3 = 性能（WP9-WP10）。
每批回归+复审后推进。

## Batch-1 正确性（用户可见错误结果）

- [x] **WP1 恢复被 #1516 静默回滚的 #1514 修复**（P1，git 考古已三重验证：
  parent1 blob 349463c91 有防护 / parent2 d2f9e3275 无 / merge 结果 = parent2；
  `git log a8d5cd510..HEAD -- <5 files>` 为空 → 安全整体恢复）
  内容：image_fusion.cpp 非对称地理参考防护 + QString::arg 日志格式修复（回滚重新引入了
  `%.6g` 动词误用 bug）、rs_sam_classify_operator.cpp O2 方言锁元数据、
  491+115+39 行测试（test_image_fusion/test_spectral_classification/test_obia_segmentation）。
  RED = 恢复前先跑恢复的测试证明其失败/缺失防护路径。
- [x] **WP2 融合读边界 ±Inf 洗涤**（P2）：readMsWindow/readPanWindow/HPF halo 仅洗有限
  sentinel；Inf 入产品（linear/brovey/PCA/GS/HPF/IHS streaming 各有路径）。对齐 band_math
  `!isfinite` 惯例。RED = 合成 Inf pan/MS 的 streaming hpf 复现 −Inf 邻居毒化。
- [x] **WP3 内存版 dos1/dos2 -Inf 毒化**（P2）：findMin 只跳 NaN → minRadiance=-Inf →
  全波段 +Inf。dos1Histogram/QUAC/streaming 均 isfinite 安全（不重复修）。
- [x] **WP4 rs_raster_spatial band 校验**（P2）：`std::max(1, getInt(band))` 吞 band=0、
  band>N 落成 GdalError 而非 InvalidParameter。改 parseBands/显式范围校验。
- [x] **WP5 linearWeighted 指针防护 + panWeight 钳制**（P3→含 P2 风险 UB）：
  补 null/OOB band 检查（对齐 brovey/pca/GS 兄弟函数）；panWeight 与 msW 对称钳 [0,1]。

## Batch-2 质量契约（可辨识状态/一致性）

- [x] **WP6 ChunkedProcessor 空/负尺寸返回 true** + Mosaic ±Inf 当数据刷入 + 融合 PCA
  零有效像素静默 false（补 errorMessage）——统一“可辨识失败”契约。
- [ ] **WP7 PCA-difference 单 NaN 毒化全图**（P2）：computePcaDifference all-or-nothing →
  逐像素传播（对齐家族其余内核）或文档化拒绝；先证实现状再定。
- [ ] **WP8 融合质量报告测试重建**：#1514 的 491 行测试恢复后（WP1），补 streaming-vs-kernel
  parity（PCA/IHS/GS/linear + NoData）、全 NoData 场景、tile 余数几何。
- [ ] **WP8b `cv::setRNGSeed(42)` 全局副作用**（P2 潜伏）：kmeans fit 换 per-call RNG 或
  记录线程契约；先写并发复现尝试。

## Batch-3 性能（输出 parity oracle 护栏）

- [ ] **WP9 多分辨率分割器 SoA 内存布局**（P1 内存/分配churn）：每像素 2×vector<double> 堆
  分配 → 平铺缓冲；输出标签 bit-exact 对照（rng 种子不变）+ 峰值 RSS 对照 3 尺寸。
- [x] **WP10 band_math_simd 每线程块 regBuffer 分配提升 + ISODATA 分裂扫描 O(k·d·n) 归约 +
  quac 双缓冲消除**：各自带 throughput/RSS 对照与输出 parity。

## 交付
- [ ] 每 WP 原子 commit；TEST_RESULTS.md / PERFORMANCE.md / LOOP_LEDGER.md 同步。
- [ ] Batch 全绿后独立 R1 → fix → R2（Oracle-F）。
- [ ] review_pack.py → push → gh pr create（不等线上 CI）。

## 记录的 FALSE POSITIVE / 不修清单
- （待补）

## 批次执行结果（2026-10-19 更新）

- Batch-1（WP1-WP5）：**全部完成**，commit 41fd31b57 / 3f2608543 / d7d7b7adc / 97d78c585。
- Batch-2 部分：WP6（三件）完成于 3f2608543 + 77802cf37；WP8（融合质量报告重建）**并入 WP1**
  ——#1514 恢复的 491 行测试即覆盖 streaming NoData 语义与 dual-lock；新增 streaming-vs-kernel
  parity（PCA/IHS/GS/linear）在 WP2 的 Inf 洗涤测试中部分覆盖（brovey×3 + hpf），全方法
  parity 矩阵列为后续维护建议。
- Batch-3：WP9 完成（285757a2f，RSS −18.8%@1024²，bit-exact）；WP10 的 quac 双缓冲完成
  （9b8976c14）；WP10 其余两项与 WP8b 缓办（见下）。

## 缓办清单（含证据与原因）

1. **WP8b `cv::setRNGSeed(42)` 全局副作用**：cv::kmeans 无 per-call RNG 注入口；
   修复并发确定性需调用点序列化（性能回退）或改自研 kmeans（新算法 family，违反
   任务书 §7）。现行为 = 串行确定性，已被 r14-A「bit-identical reruns」测试锚定。
   建议上抛 OpenCV 升级时重评。
2. **WP10 band_math_simd regBuffer 每线程块分配**（:402/:745）：提升需 thread_local 或
   OpenMP reduction 重组，AVX2 路径有 10k 点差分测试护栏但重组面较宽；本轮上下文预算
   内不冒险，审计证据保留（4k allocs/Mpix）。
3. **WP10 ISODATA 分裂扫描 O(pass·k·d·n)**（:181-186）：正确性有测试锚定，纯性能重构
   需独立基准建设（现无 ISODATA 基准），后续批次处理。
4. **WP7 PCA-difference 单 NaN 全图毒化**：`computePcaDifference` 的 all-or-nothing 是
   文档化策略还是缺陷需 D15 域裁决（其所属 change_detector.cpp 有独立验收测试
   test_change_detector 断言现行为）；未经证伪不改。
5. **SAR 地形算子 DEM 网格仅查 dims / 无取消钩子**（CF-4）：SAR 几何语义深，留 SAR 专项。
