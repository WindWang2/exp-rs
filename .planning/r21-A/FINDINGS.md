# FINDINGS — r21-A（勘查代理 + 主代理复核汇总）

来源标记：AG2=谱学/融合代理（已完成）| AG1=辐射/大气 | AG3=SAR | AG4=地形/时序 | AG5=分类/OBIA/多源 | MAIN=主代理。
所有条目修复前需主代理逐行复核（生产调用链 + RED 重现实锤）。

## AG2 — 谱学/高光谱/融合（已完成，SAM/SID/RX/MNF/GLCM/FCLS/resample/band_math 生产核确认干净）

| ID | 级 | 位置 | 缺陷 | 触发（量化） | 路径 | 修复方向 |
|---|---|---|---|---|---|---|
| AG2-D1 | P2 | image_fusion.cpp:211-262 pcaFusion | 均值用逐 band 有效集、协方差用联合有效集（流式路径 1284-1377 一致正确） | band1=[1,2,3,ND], band2=[1,2,ND,4] → cov12 内存 1.3333 vs 正确 0.5（PC1 旋转） | 测试 seam（生产走流式） | 均值改联合有效集一遍计算 |
| AG2-D2 | P2 | image_fusion.cpp linearWeighted:119/brovey:160,178/pca:220,238,330/ihs:413-452/gs:507-631/histMatch:46 | ±Inf 只测 isnan（流式 917-943 已洗为 NaN） | brovey bands={50,50,+Inf} pan=300 → 输出 {0,0,NaN} 发明黑色数据；应全 NaN | 测试 seam | 全部改 !isfinite |
| AG2-F1 | P2 | image_enhancement.cpp:1379-1390(pca)/:1471-1482(mnf) 均值 vs :1212-1232 computeCovarianceMatrix | 同 D1 结构：均值逐 band NaN 排除、协方差 listwise；且 isnan-only 门（生产 processPcaFile/processMnfFile 正确） | 同 D1 触发构造 | 测试 seam | 均值对齐 listwise-finite 集 + isfinite |
| AG2-F2 | P3 | spectral_classification.cpp:369-371 continuumRemoval | 零包络分支返回 1.0f（假"在包络上"数据）；姊妹核 analysis/hyperspectral/continuum_removal.cpp:89-91 正确 NaN | 波长[1,2,3] 谱[0,0,0] → 每波段 out=1.0，期望 NaN | **生产**（rs:continuum_removal, rs_operators_init.cpp:152） | 退化分支发 nodata/NaN |
| AG2-F3 | P3 | spectral_unmixing.cpp:142-154 OLS | 全部 LS 丰度≤0 时 clip 后 sum=0，静默输出全 0（违反头文件 sum-to-one 契约；FCLS 路径有约束） | 端元[1,1] 像元[-2,-2] → 输出 {0.0} sum=0 | **生产默认**（rs:spectral_unmixing 默认 method=ols, operator:123） | sum≤eps 时发 NaN（带 QA 标记） |
| AG2-F4 | P3 | analysis/hyperspectral/continuum_removal.cpp:147-159 | 半深交叉线性外推出 run 边界（left==start 时 t<0） | normalized=[0.2,0.1,0.15] depth=0.9 → leftHalf 外推到 λ[start] 之外 | 测试 seam | 交叉点 clamp 到 run 边界 |
| AG2-F5 | P3 | image_enhancement.cpp:29 linearStretch / :104-107 histogramEqualize | ±Inf 只测 isnan → Inf 经 clamp 变 255 假数据 / 恒值路径写 128 | +Inf 输入 → 输出 255（期望 NoData） | 面板/测试 seam | !isfinite |
| AG2-F6 | P3 | image_fusion.cpp:30-39 histogramMatch | 总体标准差(N) vs 流式样本标准差(N-1)（math_utils.cpp:134 vs StatsAccumulator:677-681） | 4 像素 tile → 尺度差 6.7% | 测试 seam | 统一估计量 |

注：AG2-D1/D2/F1/F4/F5/F6 为 seam 级（生产流式路径正确）；AG2-F2/F3 为生产路径实错。

## AG1 — 辐射/大气（待返回）
## AG3 — SAR/InSAR（待返回）
## AG4 — 地形/时序（已完成；Horn/D8/D∞/填洼/Strahler/geomorphon/Whittaker/LDLᵀ/Sen/Gilbert/调和/物候/断点确认干净）

| ID | 级 | 位置 | 缺陷 | 触发（量化） | 路径 | 修复方向 |
|---|---|---|---|---|---|---|
| AG4-1 | P2 | temporal_calendar.cpp:293-345 | regularizeSeries WindowMean 只平均最近左右 2 个有限观测（left/right 锚点），非窗口内全部；且不对称（等距丢左保右）；validObservations 少报 | obs days {0,8,16,24,32} vals {1,1,1,5,5} t=16 radius=16 → 实际 (1+5)/2=3.0/2obs，期望 13/5=2.6/5obs | **生产** rs:temporal_regularize | 窗口边界由天数轴 lowerBound 推导，窗口内全量循环 |
| AG4-2 | P3 | rs_temporal_monitor_operator.cpp:301 | sd==0 像素喂 z=0 进 CUSUM/EWMA（契约要求跳过无效观测）：恒定像素 drift=0.5 60 景 → S=−30 假警报 | 见触发 | **生产** rs:temporal_monitor | sd>0 才 step |
| AG4-3 | P3 | terrain_solar.cpp:188 | dayOfYear 366（闰年 12/31，QDate::dayOfYear 会产生）拒绝；姊妹 solar_geometry 接受 1..366 | day_of_year=366 → 中途 throw | **生产** rs:terrain_solar | 接受 366 并在 Spencer 级数内 clamp 到 365 |
## AG5 — 分类/OBIA/多源（待返回）

## AG5 — 分类/OBIA/多源（已完成；accuracy/hungarian/confusion/pansharpening/seamline/change 内核确认干净）

| ID | 级 | 位置 | 缺陷 | 触发（量化） | 路径 | 修复方向 |
|---|---|---|---|---|---|---|
| AG5-1 | P2 | rs_kmeans_operator.cpp:331-351 | `scale` 参数在 scaler 已 fit+transform 之后才读取——scale=false 与 true 输出逐字节相同；schema 承诺可关 | band1∈[300,310] band2∈[0.1,0.9]：raw 下 A=(302,0.2),B=(302,0.8) 同簇；z-score 后 d' 相等 → 划分改变 | **生产** rs:kmeans | 先读 scale 再门控 fit/transform |
| AG5-2 | P2 | rs_classification_pipeline.cpp:1216-1227 | 校准门只比对 classIds.size()==probs.cols，不比对类 id 本体；calibration={1,2,3} 配 RF 训练 {1,2,4} → 第 3 列概率/不确定性/meanConfidence 全静默错值 | 见触发 | **生产**（Config::calibrationModel + sidecar v2 路径） | 比对 backend->classOrder() 逐 id 相等，否则 failWithPartialOutput |
| AG5-3 | P3 | rs_object_classify.cpp:135-141 | OBIA kmeans Hungarian 重映射未映射簇保留 raw cluster id；像素管线同核写 unclassified（pipeline:1270-1272）——同算法双所有者策略相反 | k=5 训练类{1,2}：簇 4/5 无训练段 → 输出类 id 4/5（超出训练标签空间且计入 classes 统计） | **生产** rs:obia_classify/hierarchy | value(raw, 0) 镜像管线策略 |
| AG5-4 | P3 | mosaic_balancing.cpp:444-451 | 增益异常门只查每边 fit（注释/变量名 totalGain 承诺累计）；链 ref→A→B→C 每跳 1.25 全过门，累计 1.95× 参考辐射仍"balanced" | maxGain=1.25, 链 3 跳 | **生产** rs:quality_mosaic | BFS 链累计增益并门控（或修正注释并改语义为 per-edge 显式化） |
| AG5-5 | P3 | rs_classifier_random_forest.cpp:91-112 | 注释承诺校验 getVotes 第 0 行类 id 与捕获标签一致，实际只比 count；潜在 guard 缺口（当前 OpenCV 行为恰好一致） | 外部 sidecar/异构 OpenCV | 生产（潜在） | 实现行 0 逐列 id 校验，失配 bail out |

## AG1 — 辐射/大气（待返回）
## AG3 — SAR/InSAR（待返回）
## AG4 — 地形/时序（已完成；Horn/D8/D∞/填洼/Strahler/geomorphon/Whittaker/LDLᵀ/Sen/Gilbert/调和/物候/断点确认干净）

| ID | 级 | 位置 | 缺陷 | 触发（量化） | 路径 | 修复方向 |
|---|---|---|---|---|---|---|
| AG4-1 | P2 | temporal_calendar.cpp:293-345 | regularizeSeries WindowMean 只平均最近左右 2 个有限观测（left/right 锚点），非窗口内全部；且不对称（等距丢左保右）；validObservations 少报 | obs days {0,8,16,24,32} vals {1,1,1,5,5} t=16 radius=16 → 实际 (1+5)/2=3.0/2obs，期望 13/5=2.6/5obs | **生产** rs:temporal_regularize | 窗口边界由天数轴 lowerBound 推导，窗口内全量循环 |
| AG4-2 | P3 | rs_temporal_monitor_operator.cpp:301 | sd==0 像素喂 z=0 进 CUSUM/EWMA（契约要求跳过无效观测）：恒定像素 drift=0.5 60 景 → S=−30 假警报 | 见触发 | **生产** rs:temporal_monitor | sd>0 才 step |
| AG4-3 | P3 | terrain_solar.cpp:188 | dayOfYear 366（闰年 12/31，QDate::dayOfYear 会产生）拒绝；姊妹 solar_geometry 接受 1..366 | day_of_year=366 → 中途 throw | **生产** rs:terrain_solar | 接受 366 并在 Spencer 级数内 clamp 到 365 |

## AG1 — 辐射/大气/地形校正（已完成；BRDF/topographic/solar/radiometric 主核确认干净）

| ID | 级 | 位置 | 缺陷 | 触发（量化） | 路径 | 修复方向 |
|---|---|---|---|---|---|---|
| AG1-1 | P2 | atmospheric_correction.cpp:804(同 :641/:707) + operator :152,238-252 | DOS2 airmass 无域校验：<=0/非有限 → transmittance=0 → 全部有效像素 NaN，run 返回 success 且盖 surface_reflectance 章（内存 dos2() :292 有正确门，流式绕过） | airmass=0/-1/1e30 | **生产** rs:atmospheric_dos2 | operator 层 typed refusal（>0 且有限，上限~40）+ 内核门镜像 |
| AG1-2 | P3 | fast_6s_lookup.cpp:75 | Rayleigh 相函数系数 1+δ 应为 1+3δ（Hansen&Travis/6S 标准）；现式积分 0.960≠1，P(90°) 低 5% | λ=450nm θs=θv=30°: 路径反射率低 ~0.003 → BOA 偏 +0.004 | 测试 seam（无生产调用方） | 1.0+3.0*kDepolarization |
| AG1-3 | P3 | radiometric_calibration.cpp:930-932 + fast_6s_lookup.cpp:313-323 | `sinElevation<=0` 门放行 NaN（返回 true 全 NaN，违反头文件 fail-closed 契约）；`esen>0` 门放行 +Inf → boa 全 0 场景 | sunElevationDeg=NaN / esun=Inf | 测试 seam | 全部改 `!(x>0)||!isfinite(x)` 拒绝 |
| AG1-4 | P3 | rs_radiometric_calibration_operator.cpp:125-130 | bands 数组非整数项静默丢弃 → 空=全波段回退，scope 悄悄扩大 | bands=[2.5] on 7 波段 → 7 波段全定标 | **生产** rs:radiometric_calibration | 非整数项 typed refusal |

## AG3 — SAR/InSAR（已完成；dB/相干/相位链/解缠/Pauli/Cloude/Freeman/Yamaguchi/配准/基线/ENL/网络反演确认干净）

| ID | 级 | 位置 | 缺陷 | 触发（量化） | 路径 | 修复方向 |
|---|---|---|---|---|---|---|
| AG3-1 | P2 | rs_sar_geocode_operator.cpp:335-337,397 / rs_sar_terrain_masks_operator.cpp:275-285 / rs_sar_remove_topographic_phase_operator.cpp:476-477 | SICNU_DEM_UNIT 只被 flatten 家族尊重；geocode/masks/remove_topo 把原始 DEM 值当米 | DEM=3000 feet（真高 914.4m）→ 高度错 +2085.6m → 零多普勒斜距错 1708m ≈ 2083 列 @0.82m/采样 | **生产** rs:sar_geocode/terrain_mask/remove_topographic_phase | 读 SICNU_DEM_UNIT，非米单位按 flatten 家族同款缩放或 typed refusal |
| AG3-2 | P2 | sar_terrain.h:16,44 + 两个 operator purpose 文案 + sar_terrain_geometry.cpp:71-80 terrainRadiometricFactor | 生产实现 sinθi/sinθ0（#1146 正确）；头文件/operator 文案/exposed 内核仍写并实现 cosθ0/cosθi（被 #1146 判为有 tanθ0/tanθi 偏差的旧式）；同 token gamma0 三处矛盾，差 2~4× | θ0=35°,θi=15°: 生产 0.219 vs 契约/内核 0.435 | **生产文档+exposed 内核**（terrainRadiometricFactor 无生产调用方，被 test_sar_foundation5:100 钉住） | 全部对齐 sinθL/sinθ0：头文件/文案更正 + 内核重实现 + 测试更新 |
| AG3-3 | P3 | rs_sar_interferogram_operator.cpp:212-226 + sar_insar.cpp:127-133 | flattenRamp 拟合的是 wrap 后 (−π,π] 相位：多条纹 flat-earth 斜坡拟合≈0 静默不移除 | 1024px 2 周期斜坡 → rampCoefficients≈[0,0,0] 成功返回 | **生产** rs:sar_interferogram flattenRamp | 前置条件文档化+验证（post-fit phaseDiscontinuityRatio 高或 wrap 残差近均匀则 typed 诊断） |
| AG3-4 | P3 | rs_sar_terrain_flatten_operator.cpp:164-177 | legacy look_azimuth 别名无 isfinite 检查（主别名有）→ NaN 方位 → gamma=NaN 且 mask=1"valid" 违反 mask 契约 | look_azimuth=NaN | **生产** rs:sar_terrain_flatten | 别名分支补同款 isfinite refusal |

## AG4 — 地形/时序（已完成；Horn/D8/D∞/填洼/Strahler/geomorphon/Whittaker/LDLᵀ/Sen/Gilbert/调和/物候/断点确认干净）

| ID | 级 | 位置 | 缺陷 | 触发（量化） | 路径 | 修复方向 |
|---|---|---|---|---|---|---|
| AG4-1 | P2 | temporal_calendar.cpp:293-345 | regularizeSeries WindowMean 只平均最近左右 2 个有限观测（left/right 锚点），非窗口内全部；且不对称（等距丢左保右）；validObservations 少报 | obs days {0,8,16,24,32} vals {1,1,1,5,5} t=16 radius=16 → 实际 (1+5)/2=3.0/2obs，期望 13/5=2.6/5obs | **生产** rs:temporal_regularize | 窗口边界由天数轴 lowerBound 推导，窗口内全量循环 |
| AG4-2 | P3 | rs_temporal_monitor_operator.cpp:301 | sd==0 像素喂 z=0 进 CUSUM/EWMA（契约要求跳过无效观测）：恒定像素 drift=0.5 60 景 → S=−30 假警报 | 见触发 | **生产** rs:temporal_monitor | sd>0 才 step |
| AG4-3 | P3 | terrain_solar.cpp:188 | dayOfYear 366（闰年 12/31，QDate::dayOfYear 会产生）拒绝；姊妹 solar_geometry 接受 1..366 | day_of_year=366 → 中途 throw | **生产** rs:terrain_solar | 接受 366 并在 Spencer 级数内 clamp 到 365 |

## 处置状态（主代理复核后）

| ID | 处置 | 提交 |
|---|---|---|
| #1591 | ✅ 修复（锁+拷贝/移动+int64） | ecfdd9b |
| AG2-F2/F3 | ✅ 修复+测试 | b246a53 |
| AG5-1/2/3 | ✅ 修复+测试（AG5-3 仅代码，回归在 obia 族） | 4618e9a |
| AG1-1/2/3/4 | ✅ 修复+测试 | 62e6d22 |
| AG3-1/2/4 | ✅ 修复+测试 | 42af7b9 |
| AG4-1/2/3 | ✅ 修复+测试 | 65a2f13 |
| AG2-D1/D2/F6 | ✅ 修复+变形 oracle 测试 | a7650c2 |
| AG2-F1/F5 | ✅ 修复+测试 | ce401da |
| AG5-5, AG3-3 | ✅ 修复/诊断+schema 文档 | b43247d |
| AG2-F4 | ✅ 修复+测试 | 441282d |
| **AG5-4** | ❌ **复核驳回（误报）**：mosaic_balancing perBand 经 BFS 归纳本就是"该场景 raw→参考空间"的累计映射（ref=identity，子场景回归 y=parentBalance.perBand.apply(parent_raw)，即父的累计校正值），门检查的 fits.gain 已是累计增益——注释与变量名（totalGain）语义一致 | 无代码变更 |

fast_6s 水汽 0.5 系数（R20 记档）：本轮仍未获权威 RT oracle，不做物理常数改动（记 NOT-FIXABLE-WITHOUT-ORACLE）。
rs_simple_segmenter 31-bit 哈希碰撞（R20 记档）：确定性碰撞、影响需统计量化，本轮不触碰（记档）。
