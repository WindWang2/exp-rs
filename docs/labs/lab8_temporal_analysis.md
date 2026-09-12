# 实验8：NDVI 时序分析——趋势、物候与异常检测

> 平台能力：`rs:temporal_summary` / `rs:temporal_index_series` / `rs:temporal_trend` /
> `rs:temporal_phenology` / `rs:temporal_anomaly`（headless 管道可完整复现）

## 实验目的

1. 理解多时相遥感数据的组织方式（时间戳、逐期影像、时序栈），掌握时序分析对元数据（获取日期）的硬依赖；
2. 掌握 NDVI 时序的线性趋势建模，能解释趋势斜率的物理单位（NDVI/天）及其与采样间隔的关系；
3. 掌握物候参数（SOS/POS/EOS/生长季长度）的阈值交叉提取方法，理解采样频率对物候精度的限制；
4. 掌握基于基线 z-score 的时序异常检测，能设计「扰动期 + 扰动前对照」的验证方案；
5. 能够用 headless 管道（JSON 声明式算子链）完整复现分析流程。

## 原理讲解

### 1. 植被指数时序与物候信号

NDVI = (NIR − Red) / (NIR + Red) 对叶绿素吸收与冠层散射敏感。常绿植被 NDVI 全年高且稳定；落叶农田/草地 NDVI 呈单峰季节曲线；水体 NDVI 为负。把 12 期 NDVI 按获取日期排成序列，即可从「一张快照」升级为「过程观测」：**趋势**刻画不可逆变化，**物候**刻画周期节律，**异常**刻画突变。

### 2. 线性趋势与时间尺度

对每个像元，将 NDVI_i 对时间 t_i（该期获取日期距首期的天数）做最小二乘回归：NDVI(t) = slope·t + intercept。本平台 `rs:temporal_trend` 的斜率单位是 **NDVI/天**——它由日期差而非期数差决定，因此丢掉或错填日期会直接改变斜率数值。R² 衡量线性假设的适用度：季节性农田的年内 R² 低于单调退化的扰动地块。

- slope = Σ(t_i−t̄)(y_i−ȳ) / Σ(t_i−t̄)²
- R² = 1 − SS_res/SS_tot

### 3. 物候阈值交叉法

以季节振幅（峰值−基线）的固定比例（本实验取 20%）构造阈值，NDVI 上穿/下穿阈值以及峰值日期分别记为生长季开始（SOS）、峰值（POS）、结束（EOS），EOS−SOS 为生长季长度 LOS（天）。振幅极小（全年常绿或全年水体）的像元不构成有效季节，结果为 NoData 或退化值——这是定义的必然，不是错误。

### 4. 基线 z-score 异常检测

选定目标时相 t*，用基线窗口（本实验 2024-01-01 至 2024-08-31，即扰动前的「正常态」）逐像元估计均值 μ 与标准差 σ，异常分 z = (NDVI_t* − μ)/σ。|z| ≥ 2 通常视为显著异常。该方法的有效性依赖两个前提：基线「干净」（不含同类事件），以及**对照验证**——对扰动前时相跑同一流程，应无显著异常（假阳性控制）。

## 实验数据

| 项目 | 说明 |
|------|------|
| 影像 | 12 期 256×256 合成 Red/NIR 影像（GeoTIFF，float32，反射率 1/255 量化） |
| 时间 | 2024-01 至 2024-12，月频，每月 15 日 |
| 地物 | 常绿林地 / 农田（夏季单峰）/ 水体 / 农田内扰动地块（2024-09 起采伐裸土化） |
| 波段 | red=3、nir=4（与 `data/samples/landsat_sample.tif` 波段约定一致） |
| 数据规格 | `data/labs/data-specs/lab8_temporal_analysis.json`（离线生成，无需联网） |
| 本地临时数据 | `python3 scripts/gen_lab_fixtures.py temporal --out data/labs/_tmp`（gitignored，不入库） |

## 实验步骤

> 本实验同时提供 GUI 与 headless 两种操作路径；判分以 headless 管道产物为准。

### 8.1 加载与体检（`rs:temporal_summary`）

1. GUI：加载 12 期影像，逐景查看图层属性中的获取日期与波段结构；
2. headless：管道步骤 `summary` 输出 nir 波段概要统计（count/均值/极值/中位数）。
3. 核对每景 `SICNU_ACQUISITION_DATE`：**时序算子不做文件名日期推断**，日期缺失会被前置检查明确拒绝（typed refusal），不会静默猜测。

### 8.2 计算 NDVI 时序栈（`rs:temporal_index_series`）

对 12 期影像按 `bands: {"red": 3, "nir": 4}` 逐期计算 NDVI，得到 12 波段时序栈（每波段携带获取日期元数据）。目视检查：常绿林全年高值，农田夏峰冬谷，水体负值。

### 8.3 线性趋势分析（`rs:temporal_trend`）

对时序栈逐像元回归，读取 slope 波段：

- 扰动地块：强负斜率（量级 ≤ −8×10⁻⁴ NDVI/天）；
- 常绿林/水体：接近 0（|斜率| ≤ 2×10⁻⁴）。

注意**斜率单位是 NDVI/天**，乘 365 才是「年速率」量级——这是最常见的读数错误。

### 8.4 物候参数提取（`rs:temporal_phenology`）

读取 sos/pos/eos/los（DOY/天）波段，对照农田「夏峰」先验解释空间格局；解释为什么常绿林与水体没有有效物候值。月频采样下物候精度约 ±15 天（原理见上），这是采样密度决定的诚实边界。

### 8.5 异常检测与对照验证（`rs:temporal_anomaly`，运行两次）

以 `baseline_start=2024-01-01`、`baseline_end=2024-08-31` 为基线窗口：

1. 目标时相 `target_time=2024-10-15`（扰动后）：扰动地块应出现连片 z ≤ −2 的负异常；
2. 目标时相 `target_time=2024-08-15`（扰动前对照）：全图 |z| 应 ≤ 1。

对比两幅异常图，说明检出信号不是方法伪影。

### 8.6 综合判读

把趋势、物候、异常三个结论拼成一条事件叙事：扰动地块「何时开始偏离常态、偏离多快、是否影响生长季参数」。讨论线性趋势对「季节信号 + 扰动」混合序列的局限（可用 `rs:temporal_harmonic_fit` / `rs:temporal_breakpoints` 自行验证改进）。

### Headless 运行（判分依据）

```bash
# 1) 生成本地临时数据（gitignored，实验环境由 D1 提供等价数据）
python3 scripts/gen_lab_fixtures.py temporal --out data/labs/_tmp

# 2) 运行管道（先构建 build/sicnu_geo_rs_cli，见 README）
QT_QPA_PLATFORM=offscreen build/sicnu_geo_rs_cli \
  --pipeline data/labs/pipelines/lab8_temporal_analysis.pipeline.json
```

成功标志：`Pipeline succeeded (6 steps)`，且 `data/labs/_tmp/out/lab8/` 下生成
`nir_summary.tif`、`ndvi_series.tif`、`ndvi_trend.tif`、`phenology.tif`、
`anomaly_2024-10-15.tif`、`anomaly_2024-08-15_control.tif`。

## 预期结果

| 产物 | 预期 | 判分容差 |
|------|------|----------|
| `ndvi_series.tif` | 12 波段；常绿林 0.65–0.85，水体 −0.20–0.00，农田夏冬对比明显 | 意图 T1/T2 |
| `ndvi_trend.tif` | 扰动地块 slope ≤ −8×10⁻⁴ 且 r² ≥ 0.5；林地/水体 \|slope\| ≤ 2×10⁻⁴ | 意图 T3（斜率按天） |
| `phenology.tif` | 农田 SOS 80–170、POS 170–230、EOS 230–320 DOY，LOS 100–240 天；常绿林振幅 < 0.1 或 NoData | 意图 T4 |
| `anomaly_2024-10-15.tif` | 扰动地块 anomaly 均值 ≤ −2.0 | 意图 T5 |
| `anomaly_2024-08-15_control.tif` | 全图 \|anomaly\| ≤ 1.0 | 意图 T6 |

判分意图全文：`data/labs/grading/lab8_temporal_analysis.intent.json`（判分器由 D4 实现）。

## 思考题

1. 如果把 12 期影像的获取日期全部丢掉（或错写成同一天），趋势斜率和物候参数会怎样？这说明了时序数据管理的什么问题？（提示：`rs:temporal_*` 的前置检查会拒绝缺失日期——想想为什么「宁拒绝不猜测」）
2. 趋势图上农田区域的 R² 明显低于扰动地块，为什么线性模型对这两种序列的适配度不同？用什么模型能同时刻画季节与突变？（提示：平台提供 `rs:temporal_harmonic_fit` / `rs:temporal_breakpoints` 可自行验证）
3. 月频采样下 SOS 的不确定度大约是多少？要把物候精度提高到 ±5 天，采样策略要怎么改？
4. 若基线窗口误选了包含扰动的 9–12 月，异常检测结果会怎么变？这说明异常检测的哪个环节最关键？
5. 水体 NDVI 为负、常绿林振幅近 0，二者在物候输出中都「无有效季节」——它们的机理一样吗？如何区分？

## 术语表

| 术语 | 英文 | 释义 |
|------|------|------|
| 时序栈 | temporal stack | 同一空间网格、按获取日期组织的多期影像逐波段堆叠，每波段携带获取日期元数据 |
| 物候参数 | phenology metrics | SOS/POS/EOS（生长季开始/峰值/结束 DOY）与 LOS（生长季长度，天）等季节节律参数 |
| 基线 | baseline | 异常检测中代表「正常态」的参考时间窗口，本实验取扰动前 1–8 月 |
| typed refusal | typed refusal | 平台对无法满足前置条件（如缺日期、网格不一致）的处理方式：带类型信息的明确拒绝，而非静默降级 |

## 诚实范围（可执行子集）

- `rs:temporal_gap_fill` 只能在既有日期间插值，不能生成规则日历（如 16 天）重采样序列；
- `rs:temporal_monitor` 需要 workspace collection 标识，不能直接消费 scenes 路径数组，故不在本实验主线；
- 月频 12 期数据的物候精度受采样密度限制（~±15 天），判分容差已据此放宽；
- 平台无 CCDC/BFAST 类联合「趋势+季节」分段模型，`rs:temporal_breakpoints` 仅提供分段斜率。

完整算子缺口清单见仓库 `ISSUES.md`。
