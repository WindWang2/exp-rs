# 实验 8B：NDVI 时序规整化、物候监测与突变时间轴（Lab 08 · Temporal Phenology Timeline）

> 面向大时空尺度时序分析的教学实验。配套自动判分入口：
> `ctest -R test_d16_temporal_phenology_e2e -j1`（`QT_QPA_PLATFORM=offscreen` 无头运行），
> 判分用例 `Lab08 auto-grading` 输出 4 项 × 25 分的逐项得分，满分 100。

## 1. 实验目标

1. 掌握 **16 天规则日历** 的时空数据立方体构建方法：非均匀采集序列如何被重采样到
   `t_k = t0 + 16k` 的规则网格上，以及 Best-Pixel / WeightedMean 两种合成策略的取舍。
2. 理解 **45 天无观测缺口守则**：窗口内无有效观测或前后观测间隔超过 45 天时必须输出
   NaN——虚假插值在南极冰盖、热带雨季云区会制造不存在的"物候"。
3. 使用 **稳健 Whittaker 平滑**（Cauchy IRLS）压制未检出的云阴影负向尖峰，理解惩罚项
   λ 从 1e-6（插值退化）到 1e4（直线退化）的谱系。
4. 以 **BFAST 简化模型**（谐波 + 分段线性联合回归、RSS 贪婪分割、F 检验 + BIC 双门控）
   定位地表覆被突变（毁林、火烧、轮作）。
5. 用 **动态相对阈值法** 反演物候参数（SOS/POS/EOS/LOS），并校验生物学时序单调性
   `SOS < POS < EOS`、`LOS = EOS − SOS`（跨年物候 +365）。

## 2. 数据与场景

运行时合成的 46 景 48×48 单波段 Float32 GeoTIFF（2020-01-01 起、16 天间隔）：

- 季节信号：年内高斯生长曲线（年内峰 doy 140，σ = 25 天），第二年初注入 **-0.35 截距
  阶跃**模拟毁林；
- 云污染：偶数景的左半幅携带不透明云（波段 2 = 1.0），构成真实的"云洞"采样几何。

## 3. 实验步骤

| 步骤 | 内容 | 判分点 |
|---|---|---|
| 1 | `TemporalCube::open` 载入场景集，检查 `sliceCount() == 46` 与日历轴 | 规整化与云掩膜过滤正确率（25 分） |
| 2 | `extractPixelSeries` 提取云污染像元，`whittakerSmoothRobust(λ=100)` 平滑 | λ 优化拟合收敛性：MAE < 0.03（25 分） |
| 3 | `PhenologyExtractor::extractDynamicThreshold(0.2)` 提取第一年物候 | SOS/POS/EOS 与闭式真值误差 ≤ 2 天（25 分） |
| 4 | `BreakpointDetector::detectHarmonicBreaks` 定位年界阶跃 | 断点命中年界 ±2 索引、幅值误差 < 0.06（25 分） |

## 4. 关键公式

- 合成权重：`Q_i = (1 − cloud_i) · exp(−(t_i − t_k)² / 2σ_t²)`，`σ_t = W/2 = 16 天`；
- 阈值比率：`Ratio(t) = (z − z_min)/(z_max − z_min)`，SOS/EOS 为 0.2 水平线性插值交叉；
- 高斯季节闭式真值：`t_cross = μ ∓ σ·√(−2 ln f)`（f = 0.2 时系数 ≈ 1.7941）；
- Whittaker 正规方程：`(W + λDᵀD)z = Wy`，五对角带状 Cholesky O(n) 求解。

## 5. 思考题

1. 若把合成窗口半宽 W 从 32 天扩大到 64 天，物候 SOS 的提取会偏向哪个方向？为什么？
2. F 检验与 BIC 各自防止哪一类断点误判？二者都通过仍可能误判吗？
3. 双季稻区为什么必须用 `extractMultiCycle` 的峰谷配对而不能做全年单季阈值？
4. 时间轴部件为何要把静态图层缓存为 QPixmap？悬停刷新为什么不能触发数据重绘？

## 6. 与既有实验的关系

`data/labs/lab8_temporal_analysis.labspec.json`（id `temporal_analysis`）是同时段的
时序分析实验（趋势/物候/异常算子族）；本实验（id `temporal_phenology_timeline`）聚焦
**数据立方体到时间轴工作台**的全链路闭环，两者互补而不重叠。
