# 实验9：SAR 相干斑抑制与变化检测

> 平台能力：`rs:sar_calibrate` / `rs:sar_speckle` / `rs:sar_change` / `rs:sar_terrain_correction`
>（headless 管道可完整复现；含平台已修缺陷 #785、#803 的回归性教学验证）

## 实验目的

1. 理解 SAR 相干斑的乘性噪声本质与等效视数（ENL）度量，掌握 Lee 滤波的自适应均值-方差估计原理；
2. 掌握 SAR 辐射定标流程（DN → σ0），理解两景影像必须使用同一校准常数才能可比；
3. 掌握 dB 对数比 + Otsu 自动阈值的双时相变化检测方法，能报告检出率与虚警率；
4. 理解同极化配对与严格配准作为变化检测的前置条件；
5. 通过 #785（视角几何）与 #803（多波段 NoData）两个已修复缺陷的复现实验，理解传感器几何与 NoData 语义对 SAR 处理链的正确性意义。

## 原理讲解

### 1. 相干斑：乘性噪声与统计模型

SAR 以相干波成像，分辨单元内多个散射子的回波相干叠加，形成颗粒状相干斑。单视强度像元服从指数分布，L 视处理后服从 Gamma 分布（均值 σ0，方差 σ0²/L）。等效视数 **ENL = mean²/var** 是均匀区相干斑强度的直接度量：L 视数据 ENL ≈ L。滤波器的任务是在压斑（提 ENL）的同时保持辐射（均值不偏）与纹理（边缘不糊）。

- I = σ0 · X，X ~ Gamma(L,1)/L（L 视强度斑）

### 2. Lee 滤波：局部线性最小方差估计

Lee 滤波假设窗内先验均值与方差可由窗口统计估计，在最小均方误差意义下对中心像元作线性组合：**î = ī + b·(I − ī)**。均匀区 b→0（输出趋近窗口均值，强压斑），边缘区 b→1（保留原始值，保边）——这是它优于简单均值滤波的原因。5×5 窗口在 4 视数据上是常用起点：窗口越大压斑越强，但分辨率损失越大。

### 3. 辐射定标与 dB 对数比

传感器 DN 与后向散射的换算（平台公式，见 `src/processing/algorithms/sar/sar_calibration.cpp`）：**σ0 = (DN² − noise) / A²**（本实验 noise=0，A=100）。两景必须用同一 A 定标，否则幅度系统差会被误判为变化。变化检测在 dB 域做对数比：**Δ = 10·log10(σ0_B/σ0_A)**——乘性斑在对数域近似加性高斯，比值对称（±Δ 分别对应增强/减弱）。Otsu 法在 Δ 直方图上自动求类间方差最大的阈值。

注意：向默认 `inputDomain`（linear_power）喂 dB 数据会被平台 **typed refusal** 拒绝——域声明错误是 SAR 处理链最常见的静默杀手。

### 4. 视角几何与 NoData 语义：两个已修缺陷的教学意义

- **#785（视角方位角，已修复）**：地形校正把斜距 σ0 投影到地面（γ0），需要雷达视线方位角 = 航向角 ± 90°（视向 side 决定 ±）。修复前 UI 与几何模块对「哪一侧」的口径不一致，山地 γ0 会系统性偏差。修复后平台明确：`headingDeg` 与 `lookDirection` 组合推导，显式 `lookAzimuthDeg` 才可覆盖，**UI 不得自动填**。平地上填错视向几乎看不出影响、山地上却产生无报错的系统偏差——这正是「隐式语义」的危险。
- **#803（多波段 NoData，已修复）**：滤波器在多波段模式下曾把单波段的 NoData 标记扩展到整个数据集，导致另一波段的有效像元被误置 NoData。修复后**逐波段**解析 NoData 哨兵。SAR 数据中 NoData（叠羽/阴影区）是常态，这条修复决定多极化/多时相栈处理是否可信。

## 实验数据

| 项目 | 说明 |
|------|------|
| 影像 | 同极化 VV/VV 配对 DN 影像（2025-03-01 / 2025-04-15），256×256，float32，σ0 = (DN/100)² |
| 相干斑 | 4 视强度相干斑正演（水体区 ENL ≈ 4） |
| 地物 σ0 | 水体 −22 dB、森林 −16 dB、农田 −12 dB、裸土 −14 dB |
| 变化 | 后时相森林内 40×40 采伐亮斑（−8 dB，共 1600 像元，占总幅 2.4%） |
| 辅助 | 同网格 DEM（100–800 m 缓丘）、双波段 NoData 演示栈、变化真值码图 |
| 数据规格 | `data/labs/data-specs/lab9_sar_processing.json`（离线生成，无需联网） |
| 本地临时数据 | `python3 scripts/gen_lab_fixtures.py sar --out data/labs/_tmp`（gitignored，不入库） |

## 实验步骤

> 本实验同时提供 GUI 与 headless 两种操作路径；判分以 headless 管道产物为准。

### 9.1 辐射定标（`rs:sar_calibrate`）

对前/后时相 DN 影像用同一校准常数 `calibrationA=100` 定标到 σ0（`outputDomain=linear_power`）。检查统计：水体区 ≈ −22 dB，森林区 ≈ −16 dB。**两景必须同常数**，否则幅度系统差会被当成变化。

### 9.2 相干斑抑制（`rs:sar_speckle`，Lee 5×5）

对两景 σ0 各做 Lee 5×5 滤波。在水体均匀区计算 ENL，对比滤波前后（应提升 ≥ 1.5×）并检查均值保持（|Δ| ≤ 10%）。

### 9.3 变化检测（`rs:sar_change`，Otsu）

以滤波后两景为输入（**同极化配对 + 严格配准是硬前置条件**），`inputDomain=linear_power`，`thresholdMethod=otsu`，输出 UInt8 变化掩膜。对照真值码图计算检出率（≥ 65%）与虚警率（≤ 2%）。

### 9.4 多波段 NoData 验证（#803）

对双波段 DN 栈（band2 含左上 20×20 NoData 洞）执行 `band=0` 全波段 Lee 滤波。验证：band2 的洞保持 NoData（NaN），band1 全幅有效——修复前 band1 会被 band2 的 NoData 污染。

### 9.5 视角几何验证（#785）

对前时相 σ0 + DEM 做地形校正：`incidenceDeg=35`、`headingDeg=190`、`lookDirection=right`（视线方位角 = 190+90 = **280°**）、`flagIncidence=true`。检查输出含 γ0、有效掩膜与本地入射角波段。

### 9.6 综合判读

把变化掩膜叠加回影像，结合 dB 幅值解释变化方向（本实验为采伐迹地变亮：残余双次散射）。讨论：若跳过滤波直接做变化检测，Otsu 阈值会被斑噪声推向何处？

### Headless 运行（判分依据）

```bash
# 1) 生成本地临时数据（gitignored，实验环境由 D1 提供等价数据）
python3 scripts/gen_lab_fixtures.py sar --out data/labs/_tmp

# 2) 运行管道（先构建 build/sicnu_geo_rs_cli，见 README）
QT_QPA_PLATFORM=offscreen build/sicnu_geo_rs_cli \
  --pipeline data/labs/pipelines/lab9_sar_processing.pipeline.json
```

成功标志：`Pipeline succeeded (7 steps)`，且 `data/labs/_tmp/out/lab9/` 下生成
`sigma0_before.tif`、`sigma0_after.tif`、`sigma0_before_lee5.tif`、`sigma0_after_lee5.tif`、
`change_otsu.tif`、`stack_lee_allbands.tif`、`gamma0_before.tif`。

## 预期结果

| 产物 | 预期 | 判分容差 |
|------|------|----------|
| `sigma0_before.tif` | 水体 0.005–0.008、森林 0.020–0.032，无负值 | 意图 S1 |
| `sigma0_before_lee5.tif` | ENL ≥ 1.5×原始，均值保持 ±10% | 意图 S2 |
| `change_otsu.tif` | 检出率 ≥ 65%，虚警 ≤ 2%，changedPercent ∈ [1,6]% | 意图 S3 |
| `stack_lee_allbands.tif` | band2 NoData 洞保持、band1 不被污染（#803） | 意图 S4 |
| `gamma0_before.tif` | 含 γ0/有效掩膜（+本地入射角），缓丘有效 ≥ 98%；γ0/σ0 比值 ∈ [0.5, 3.5]（DEM 带起伏，高于平地基准 1/cos35°≈1.22） | 意图 S5 |

判分意图全文：`data/labs/grading/lab9_sar_processing.intent.json`（判分器由 D4 实现）。

## 思考题

1. 为什么 SAR 相干斑是乘性噪声而光学传感器噪声近似加性？这决定了滤波器设计（Lee 的线性模型）的什么假设？
2. 把 kernelSize 从 5 改到 9，ENL 与边缘保真各怎么变？「压斑」与「保细节」的取舍在什么应用里偏向哪边？
3. 若两景用了不同校准常数（A=100 与 A=80），对数比变化图会出现什么伪影？如何用直方图发现？
4. #785 修复为什么坚持「UI 不得自动填充 lookAzimuthDeg」？自动填充在什么地形下会造成无法察觉的错误？
5. 平台拒绝「declared-dB 输入 + 默认 linear_power 域」的组合（typed refusal）。类比 #803，为什么「宁可拒绝、不可猜测」在 SAR 链里特别重要？

## 术语表

| 术语 | 英文 | 释义 |
|------|------|------|
| 相干斑 | speckle | 相干波成像中散射子相干叠加造成的颗粒状乘性噪声，L 视强度服从 Gamma 分布 |
| 等效视数 | ENL | 均匀区 mean²/variance，度量相干斑强度；L 视处理理论上 ENL ≈ L |
| σ0 / γ0 | sigma-naught / gamma-naught | 后向散射系数的两种归一化：σ0 相对水平地面投影面积，γ0 相对垂直于视线的平面投影面积 |
| 同极化配对 | like-polarization pairing | 变化检测要求两景同极化通道（如 VV/VV）且严格配准 |
| typed refusal | typed refusal | 平台对无法满足前置条件的处理方式：带类型信息的明确拒绝，而非静默降级 |

## 诚实范围（可执行子集）

- 平台无极化分解（H/A/α、Freeman-Durden、Cloude-Pottier）；`rs:sar_dualpol_features` 仅提供双极化特征 raster；
- `rs:sar_change` 严格双时相；多时相变化统计走 `rs:sar_temporal_stats`，其 `argmax_date` 输出是 **0-based 场景索引**而非日历日期；
- `rs:topographic_correction` 是光学专用（太阳角），不适用于 SAR；SAR 地形校正走 `rs:sar_terrain_correction` / `rs:sar_terrain_flatten`；
- 本主线采用 定标 → 滤波 → 变化；山地场景规范顺序应为 定标 → 地形校正 → 滤波 → 变化，教学步骤 9.5 单列以便对照 γ0 与 σ0。

完整算子缺口清单见仓库 `ISSUES.md`。
