# TASKS — r21-A

里程碑：M0 基线 → M1 预防性测试/契约 → M2 修复 → M3 性能 → M4 功能深化 → M5 端到端 → M6 反向审查 → M7 交付。
每项先 RED（失败测试/最小重现实锤）再改，RED/GREEN 记账于 RED_GREEN.md。

## WP-A0 历史闭环（O-A1）

- [x] A0.1 LIVE 盘点 + R20 #1671 七契约在位实读（BASELINE.md）
- [ ] A0.2 七族回归两遍绿（构建完成后）：d16_temporal_trend / terrain_viewshed / mosaic_plan / classifier_mlp / classifier_isodata / sar_kernels / sar_insar
- [ ] A0.3 FIXED-OPEN issue 回归佐证（1550/1538/1590/1631/1646-6 相关测试族）

## WP-A1 科学正确性波次（每域 ≥1 实错修复 + 契约增强）

- [ ] A1.1 辐射/大气/定标：radiometric_calibration、atmospheric_correction、fast_6s、brdf、topographic_correction 审查（含 R20 记档 fast_6s 0.5 系数再评估）
- [ ] A1.2 指数/谱学/高光谱：spectral_index、sam/sid、mnf、pca、rx、unmixing、continuum removal、spectral_resample（含 image_fusion 内存 seam 两项 R20 记档）
- [ ] A1.3 SAR/InSAR：backscatter、calibration、insar、speckle、polsar、terrain correction（不回退 #1671 契约）
- [ ] A1.4 地形水文：terrain_analysis/flow/hydrology/solar/viewshed（守恒/拓扑/可见性解析 oracle）
- [ ] A1.5 分类/OBIA：kmeans/isodata/mlp/svm/rf/normalbayes、post_process、accuracy、uncertainty（含 #1591 竞态修复）
- [ ] A1.6 时序：temporal_*（summary/composite/index_series/trend/anomaly/extract_series/gapfill/fit）不规则采样/季节缺测/时间戳冲突
- [ ] A1.7 多源链：mosaic、pansharpening、band_math、change_detection、post_classification 对齐与转移统计

## WP-A2 实用化/质量（O-A4/O-A5 支撑）

- [ ] A2.1 至少一处可量化性能改进（tile 边界重复计算/大数组拷贝/缓存无界），数值等价前提
- [ ] A2.2 typed error/QA 元数据一致性抽查（只读本轨；跨域记 CROSS_TRACK）
- [ ] A2.3 3 条端到端工作流实跑（光学→指数→分类；时序→趋势；SAR→变化/InSAR 质控）

## 状态图例

- [ ] 未开始 / [x] 完成（附证据）/ [~] 部分 / [!] 阻塞（附原因）
