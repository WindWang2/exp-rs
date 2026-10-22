# REVIEW_R1 — zcode-r20-01

评审员：独立 fresh reviewer（general-purpose 代理，未参与实现），2026-10-23。
对象：worktree 全部未提交改动（15 文件，8 src + 7 tests）。
方式：逐行读 diff + 调用方追踪 + **实际构建并运行 21 个相关测试目标** + 数值复算（F-06 边界、F-07 代数手推）。

## 总体结论：需修复后合并 → 已修复

- **7 项内核修复全部判定正确**（F-07 backConvert 展开代数逐项手工复核通过；F-02 调用方逐一核实零行为回归；F-04 消费方 min/maxClassId 与 sidecar 无失配）。
- 范围检查：无范围外改动；tests diff 纯增量。
- 实测回归：21 个相关目标全部通过（test_d16_temporal_trend 84 断言、test_terrain_viewshed 1453、test_mosaic_plan 101、test_sar_insar 284、test_classifier_mlp 16、test_classifier_isodata 461、test_quality_mosaic_operator 2002、test_mosaic_blend 5072、test_classification_pipeline 240、test_classifier_load_save 623 等）。

## Findings 与处置

| # | Sev | 内容 | 处置 |
|---|---|---|---|
| R1-1 | **P0** | test_sar_kernels.cpp 用限定名 `sicnu::convertBackscatterRaster` 等，但符号在 `sicnu::sar::` 且限定名不走 using-directive → 目标编译失败（12 个既有用例全丢、F-06 零覆盖）；此前增量构建"exit 0"被 grep 管道掩盖 | ✅ 已修：去限定名（走文件顶部 using-directive） |
| R1-2 | **P1** | 同测试 89.99° oracle 用 double 字面量，但入射角栅格存 Float32（89.98999786），gamma0 差 1.224 > margin → 编译修好后必挂 | ✅ 已修：oracle 用 `double(float(theta))` 量化角 + margin 1e-3 |
| R1-3 | P3 | 常量角子测试 95° 无区分度（旧代码也被负值门拦）；90.0° 才是判别探针 | ✅ 已修：探针改 90.0 |
| R1-4 | P3 | operator 常量角 ≥90 只在 kernel 逐像元拒绝，operator 缝隙产出"成功"全 NaN 产品且元数据仍写毒角 | ✅ 已修：rs_sar_backscatter_operator.cpp 参数校验 (0,90) typed refusal（本轨允许区 src/operators/rs） |
| R1-5 | P3 | dense_linalg 绝对主元阈值 1e-12 对近奇异（非精确奇异）输入仍可出巨系数（既有属性，中心化已显著改善未消除） | 📝 记档：primitives 共享求解器，改阈值影响面广，不在本 PR 动 |
| R1-6 | P3 | F-01 测试子用例 2（缺失帧在排序轴末端）与 pval/z 断言无区分度（slope 主断言有） | 📝 保留（审者自评"无害"）：作为文档性断言 |
| R1-7 | Info | F-05 守卫先于 X.empty() 报标签错（报错语义顺序变化） | 📝 不修（功能等价，旧代码同样先读 y） |
| R1-8 | Info | θ=0 对 gamma0 拒绝略过头（cos0=1 良定义；仅 beta0 有极点）——统一 (0,90) 与 terrain_geometry 及头文件契约一致 | 📝 设计选择，不改 |

## 修复验证

R1-1/2/3/4 修复后重建 test_sar_kernels + test_sar_operators 系（见 TEST_RESULTS.md 实测记录）。
