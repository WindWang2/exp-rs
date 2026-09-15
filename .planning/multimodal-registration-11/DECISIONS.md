# DECISIONS — multimodal-registration-11

## D-001 Rescope：从"重写配准"改为"补缺口"
GOAL prompt 生成时假设 D14 之后需要"跨模态自动匹配、层级优化、RPC/DEM bias refinement、批量多景对齐、可信质量图"。Phase 0 审计确认 D14 已交付 GCP/闭式变换/TPS/RANSAC/resampling/双窗 UI，但 phase correlation、MI、金字塔、模型选择、RPC bias 模型、stack registration、CE90/质量图**均不存在**（grep 0 hit）。本 track 全部落在缺口上，不重写 feature_matcher/geometric_transform/gcp_manager/resampler，只消费其稳定 API。PR #991/#992（prompt 生成时 open）启动时已合并，不再是约束。

## D-002 新代码放 `src/processing/algorithms/registration/`（新子目录）
候选：(a) 平铺进 `src/processing/algorithms/`；(b) 新子目录 `registration/`。
选 (b)：GOAL write scope 显式含 `*registration*`；`sar/`、`temporal/`、`detail/` 已确立子目录先例；平铺会把 8 个新文件混入 60+ 既有文件目录，增加并发 PR 冲突面。

## D-003 Phase correlation 自研（FFT），不引 OpenCV 到 processing 层
候选：(a) OpenCV `phaseFocus`/`cv::phaseCorrelate`；(b) 自研 FFT-based cross-power-spectrum phase correlation。
选 (b)：`src/processing` 层现有算法零 OpenCV 依赖（OpenCV 只出现在 app 层 rs_sift_matcher，且是 optional）。自研 2D FFT（迭代 radix-2 + 任意尺寸 blankman/hann 窗 + 补零到 2 的幂或复用混合基）规模可控（配准窗口 ≤1024²），并保持离线/跨平台无新增依赖（GOAL envelope 禁新增重量级依赖）。实数 FFT 用复数 FFT 实现（N/2 折叠可后续优化，正确性优先）。

## D-004 MI 用联合直方图（8×8 bins, 16-bit quantization）
候选：(a) kNN-MI（Kraskov）；(b) 联合直方图熵估计。
选 (b)：窗口级（~32²-64² 样本）下直方图 MI 是标准做法、确定性强、可手算 oracle（已知联合分布的解析 MI 上界）。8 bins/通道量化对光学-SAR 窗口匹配足够，且负值/NoData 样本可显式剔除。

## D-005 跨模态 patch 描述子：log-gradient 方向直方图 + RANK/排序特征，不伪装成 SIFT
候选：(a) 扩展现有 SIFT-like 描述子；(b) 新描述子：亮度对数压缩 + 主梯度方位直方图 + 排序（RANK）一致特征。
选 (b)：光学-SAR 的辐射差异使强度直方图描述子失效；log-RAD 压缩抑制 SAR 乘性斑点动态范围、梯度方位对边缘/结构稳定、RANK 特征对单调辐射变换不变。命名诚实（`MultimodalPatchDescriptor`），文档明确"不是通用 SIFT 替品"。

## D-006 模型选择：holdout（k=4 折交叉验证）+ 改进门限 + κ 门限，不用 AIC
候选：(a) AIC/BIC；(b) k-fold CV RMSE + 相对改进门限。
选 (b)：配准残差有空间相关（不是 iid 假设），IC 权重不可靠；CV RMSE 直接可解释（"held-out 点上的 RMSE"），并自然产生 per-model 证据表供 explain。门限：更复杂模型须使 held-out RMSE 相对改进 ≥ 10% 且 κ < 1e14，否则拒绝（过拟合拒绝）。最少点数硬门限照搬现有 `minGcpCount` 语义。

## D-007 RPC bias 模型：constant → affine（ground space）两级，CV 选择 + 高度敏感性
既有实现只做常数 lon/lat 中位数偏置。升级为：在 destination CRS 平面上拟合 bias（constant 或 6 参 affine），k-fold CV 决定是否值得升级到 affine；affine 仅在 ≥6 个 GCP 且 held-out 改进 ≥10% 时启用，否则回退 constant（fail-closed 到已验证行为）。高度敏感性：对每个 GCP 用 DEM/常数高程 ±Δh 重投影，报告 dGround/dH（m/m），进 quality report。**不改 GDAL RPC 系数本身**（沿用"不折入 dfLONG_OFF"的既有决定，ADR 0057 语义）。

## D-008 Stack registration：平移/仿射全局最小二乘，不做 bundle block adjustment 全家桶
候选：(a) 完整 BBA（含 RPC 系数求解）；(b) pair-graph + 每对平移/仿射约束 + 全局 LS + 闭环残差诊断。
选 (b)：本 track 的多景对齐目标是栅格级对齐（后续 mosaic/cube 消费），(b) 复用 GeometricTransform 闭式解，全局解是稀疏线性 LS（对平移是解析的；对仿射用正规方程），闭环误差天然成为 drift 诊断。BBA 明确 not-supported 并写入 CAPABILITY_MATRIX。

## D-009 CE90：经验分位数优先
候选：(a) bivariate-normal 假设 CE90≈2.146σ；(b) 残差径向误差经验 90% 分位数。
选 (b) 为主、(a) 为参考值同时报告：GCP 残差常有厚尾/聚集，经验分位数无分布假设；样本 <20 时标注 `degraded: sampleTooSmall` 并回退报告 σ 法 + 明示假设。

## D-010 agent surface：注册既有 `spatial:geometric_registration` + 新增 `multimodal_register`/`stack_register` action
修复 master 上的接线缺口（geometric_tool 编译但未注册，见 BASELINE）。不新建第二个工具 ID，沿用既有工具名（避免 tool catalog 分裂）。新增 action 返回结构化 explain JSON（per-model CV 表、匹配统计、refusal 原因）。

## D-011 `rs:` 算子命名：`rs:register_images` / `rs:stack_register`
候选：`rs:georef:*`（与 GUI module: georef 冲突语义）、`rs:registration` 单算子多模式。
选两个窄算子：与 `rs:sar_coregister` 等既有命名一致（动词_对象），一算子一职责；headless pipeline 语义清晰。不占用 `rs:georef` 前缀（保留给未来与 GUI session 对齐的官方暴露）。

## D-012 #1005 fail-closed：`mapPickToLayerCrs` 返回 `std::optional<QgsPointXY>`
三处静默返回原点（canvas/layer 无效、CRS 无效/相等、transform throw）全部改为 nullopt；两个调用点（onSourcePointPicked/onDestPointPicked）收到 nullopt 时状态栏拒绝并中止本次 GCP 提交。CRS 相等路径保持直接返回（那是数学上安全的恒等，非失败路径）。addresses #1005；不关闭 issue。

## D-013 测试真值独立性
所有新 known-answer 测试使用手算/解析真值（已知单应、已知平移、手算 MI 上界、手算 CE90 分位数），不复用被测实现；复用既有 `warper_test_helpers.h` 的合成 RPC fixture（其真值已手推导出）。SAR-like fixture 用乘性 Gamma 斑点 + 辐射单调变换 + 结构遮挡，真值变换已知。

## D-015 RpcBiasModel 与 qgsrpcgcptransformer 的集成边界
候选：(a) qgis_analysis 链接 sicnu_processing 让 transformer 消费 RpcBiasModel；(b) 在 analysis 内复制 affine 拟合数学；(c) transformer 维持既有 constant-median 精化（11.6 语义，test_rpc_gcp_refine 钉死），RpcBiasModel（constant→affine CV + 高度敏感性）作为 processing 层权威实现交付，transformer 集成列为 follow-up。
选 (c)：(a) 造成层级反转（analysis 是 vendored 低层，且 qgis_analysis 是 STATIC，PUBLIC 依赖传播会扰动全仓 link 顺序）；(b) 违反单一真值原则。既有 constant 行为已由测试钉死且与新模型 constant 路径一致（同为 median + 改进门限）。

## D-014 网络抖动
启动时 `git fetch`/GraphQL 多次 EOF；策略：关键只读命令重试 ≤3 次；PR 创建阶段若 push 连续失败，记录 EVIDENCE 并稍后重试（终态仍要求 PR 创建成功）。
