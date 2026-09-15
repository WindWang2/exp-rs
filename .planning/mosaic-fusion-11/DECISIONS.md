# DECISIONS — F15 mosaic-fusion-11

## D-001 F 包 rescope：融合质量已在 master 覆盖大半（约束："不重复造轮子"）

**证据**：`src/processing/algorithms/pansharpening.{h,cpp}`（D14/ADR 0159，commit `89e2253b18`）已交付
GS/Brovey/IHS/HPF 四方法 + Wald `evaluateQuality`（ERGAS/CC/RMSE/SSIM）。
**决策**：F 包不重写融合算法。深化两个真实缺口：
(a) `fusion_quality_report.{h,cpp}` — Q（Wang et al. universal image quality index）、RASE、
per-band mean/std 失真比 + JSON report artifact + 可配置失真防护 gate（Q/ERGAS/meanRatio 阈值 → pass/warn/fail）；
(b) `rs:image_fusion` 算子面缺 `gram_schmidt`/`hpf` 方法（仅 linear/brovey/pca/ihs）→ 委托
`rs::algorithms::PanSharpening` 补齐，并加 `qualityReport` 参数。回归 = 既有 test_pansharpening/test_image_fusion 不破坏。

## D-002 新算子 `rs:quality_mosaic`，不扩展 `rs:mosaic` 参数面

候选：(a) rs:mosaic 加 `mode=advanced` 等参数；(b) 新算子。
**选 (b)**：rs:mosaic 契约（scientific_contract.cpp:521、capability_catalog、workflow builtin tool、GUI dialog）
均已锁定"first/last-valid merge"语义；在其上叠 seamline/balance/provenance 会改变既有契约且 review 面不可控。
新算子 additive、无行为漂移，复用同一注册 seam。命名沿用 `rs:<verb>` 规则，无编号。

## D-003 命名空间与文件布局

新算法统一 `namespace rs::mosaic`（呼应既有 `rs::algorithms`/`rs::core` 先例，ADR 0159 §Placement），
文件前缀 `mosaic_*`（匹配 GOAL primary scope glob `*mosaic*`）；
融合报告文件 `fusion_quality_report.*`（匹配 `*fusion*`）。

## D-004 跨 CRS 处理 = fail-closed + 显式 plan 诊断，不做内联重投影

候选：(a) quality mosaic 内联 GDALWarp 重投影；(b) plan 显式 inventory + fail-closed 指引先跑既有
`gdal:reproject` 算子。**选 (b)**：重投影 authority 已存在于 `gdal_reproject_operator`（不建第二真值）；
内联 warp 会引入重采样/NoData 二次语义，超出 mosaic 职责。plan（A 包）用 OGRCoordinateTransformation
把混合 CRS 足迹统一变换到 plan CRS，报告 `crsMismatch` 输入清单与一致的目标 CRS 建议，
算子对真实数据 CRS 不一致保持 InvalidInputData 硬失败（与 rs:mosaic 一致）。

## D-005 B 包（匀色）方法：overlap 图 + 稳健线性回归 gain/bias，链式 BFS 传播

候选：(a) 全局直方图匹配；(b) mean/std 匹配；(c) 稳健回归 gain/bias（trim 重叠像素按残差，
Huber 风格截断）+ overlap 图 BFS 链式传播到不与 reference 直接重叠的场景。
**选 (c)**：mean/std 匹配对 outlier 敏感（云）；直方图匹配破坏相对辐射关系且非 tile 化友好。
gain 限幅 `[minGain,maxGain]`（默认 [0.5,2.0]）、残差 MAD 阈值剔除云污染；无 overlap 路径 → 显式失败。
reference 默认 = 有效像素最多者（tie-break：输入序号小者），可显式指定。

## D-006 C 包（seamline）方法：pairwise DP 最小成本路径 + label 光栅合成

候选：(a) 全局图割（min-cut/max-flow）——需引入图割库或自实现 BK 算法，重量级；
(b) 逐重叠区 DP 最优路径（垂直/水平取向自适应）+ 确定性 tie-break —— 经典生产做法（ENVI/GDAL seamline 同族），
内存 O(overlap 宽×高) 且 tile 化友好。
**选 (b)**。成本 = w₁·|A−B|（平衡后辐射差）+ w₂·|∇A−∇B|（梯度/边缘惩罚）+ w₃·cloudPenalty
+ w₄·distanceToFootprintEdge（归一化）。tie-break：等成本取行/列号小者，再取场景序号小者（全序、可复现）。
N 场景：按优先级顺序做 label 光栅的顺序合成（后续场景的 seam 只在与其 predecessors 的重叠带内计算）。

## D-007 D 包（blending）：feather 为主，多尺度金字塔限定于接缝带窗口

**决策**：`blend=feather`（seam 两侧 blendWidth 内线性权重、权重归一防缝、不产生 NoData 裂缝）
为默认可用路径；`blend=multiband` 提供 Laplacian 金字塔融合但限定在接缝带局部窗口（bounded memory），
显式记录 halo 防护（权重和恒一、不放大高频于低权区）。不做全图金字塔（内存不受界）。

## D-008 E 包 score composite：加权评分 + 必要条件过滤，provenance 独立波段

score = w_cloud·cloudClear + w_quality·qualityRaster + w_time·timeCloseness + w_view·viewFlatness（权重和 1，
用户可配；缺省维度的权重重归一）。云掩膜/质量栅格为可选 per-input sidecar；时间=view 参照
`referenceTime`（默认最早输入）的日距衰减。provenance = UInt32 波段（0=未覆盖，i+1=第 i 输入贡献），
作为可选输出波段/sidecar，保证"每像元 source 可追溯"Oracle。合成顺序 = score 降序 + 序号 tie-break。

## D-009 G 包（atomic streaming output）：复用 OutputFileCleaner 模式 + overview/sidecar 原子化

主输出 GeoTIFF 沿用"临时路径写 → close → rename"语义（GdalDatasetWrapper + cleaner 扩展支持
creation options：TILED/COMPRESS/SPATIAL-reduction 经 GDAL 原生句柄）。
overview 构建失败 → 降级为 warning 而非失败（overview 属增强非权威数据）；
JSON report/manifest sidecar 用 temp+rename 原子写。

## D-010 H 包语料与规模证据：合成真相 + 逻辑 tile 规模 + 环境门控实图

语料生成器（tests 共享头）：种子化确定性场景（已知 gain/offset、已知云块、已知最优 seam 位置）。
规模证据双层：(1) 默认 bounded logical scale——kernels/operators 对 512² tile 的峰值工作字节计数不随
逻辑图幅（≥100k tiles）增长（合成 tile source 接口计数验证）；(2) `EXP_MOSAIC_SCALE_E2E=1` 环境门控下
跑真实 GDAL 稀疏大图小样本（不作为日常 gate）。**不把 wall-clock 当 correctness gate**（GOAL 约束）。

## D-011 GUI 面不动

`mosaic_dialog`/`fusion_dialog` 既有路径保持兼容（它们调用 rs:mosaic/ImageFusion，不受 additive 变更影响）。
新能力 surface = agent/operator registry + CLI runtime + docs。GUI 集成记为 follow-up。

## D-012 ADR 编号 = 0163

master 已有 0162（D17 workflow IR）；0158 被 PR #1008 占用（未合）。取下一个空闲 0163。

## D-013 测试文件布局（targeted suite 命名）

`test_mosaic_plan` / `test_mosaic_balancing` / `test_mosaic_seamline` / `test_mosaic_blend` /
`test_mosaic_quality` / `test_fusion_quality_report` / `test_quality_mosaic_operator`（E2E GDAL fixtures）/
`test_mosaic_scale`（逻辑规模+cancel）+ `tests/mosaic_test_utils.{h,cpp}`（合成语料 + 计数 tile source）。
全部注册进 tests/CMakeLists.txt 尾部（append-only）。
