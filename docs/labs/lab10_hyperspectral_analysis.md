# 实验10：高光谱分析——MNF 降维、PPI 端元提取、SAM/SID 匹配与线性解混

> 平台能力：`rs:mnf` / `rs:endmember_extraction` / `rs:sam_classify`（含 SAM 与 SID 两种度量）/
> `rs:spectral_unmixing`（headless 管道可完整复现）

## 实验目的

1. 理解高光谱数据的「图谱合一」特点：数十个窄波段构成连续光谱曲线，波段间强相关；
2. 掌握 MNF（最小噪声分数）变换：噪声白化 + 主成分，理解 SNR 有序分量与有效维数判定；
3. 掌握 PPI 端元提取的几何直觉（随机投影极值计数），理解端元 = 混合空间凸包顶点；
4. 掌握 SAM（光谱角）与 SID（光谱信息散度）两种匹配度量，比较其对幅度/形状差异的敏感性；
5. 掌握线性光谱解混模型与「丰度和为一」约束，能解读逐端元丰度图与重构误差。

## 原理讲解

### 1. 高光谱数据与混合像元问题

高光谱传感器以窄波段（本实验 10 通道 VNIR，450–900 nm，FWHM 25 nm）采样连续反射光谱。空间分辨率有限时，一个像元常覆盖多种地物——像元光谱近似为端元光谱的线性组合：**x = Σ f_i·e_i + n**，f_i 为丰度（物理上应有 f_i ≥ 0、Σf_i = 1）。「有哪些端元、各占多少丰度」正是本实验四个环节要回答的问题。

### 2. MNF：噪声白化的主成分

PCA 最大化总方差，但高光谱各波段噪声不同，方差最大的方向未必是信噪比最高的方向。MNF 先用噪声协方差做白化，再在白化空间做 PCA，得到**按 SNR 递减**排序的分量。前 1–3 个分量承载地物信息（本实验数据有效维数约为端元数−1=2 加噪声维），后面的分量以噪声为主。

### 3. PPI：随机投影下的极值像元

像元光谱可视为高维空间的点云：混合像元位于纯端元连线/凸包内部，纯端元位于凸包顶点。PPI 生成大量随机单位向量（本实验 1000 个），把点云投影到每个向量上，统计落入极值端的次数——顶点像元被反复选中。平台实现为流式三遍扫描，RNG 固定 `mt19937(42)`，结果可复现；**输出为 JSON**（endmembers/indices/ppiCounts），不含栅格。

### 4. SAM 与 SID：两种光谱匹配度量

SAM 把像元谱与参考谱的夹角作为相似度：**θ = arccos(⟨x,e⟩/(‖x‖‖e‖))**，对整体幅度增益不敏感（照度差异），对形状敏感。SID 把光谱当作概率分布，用 KL 散度的对称化度量差异，对分布形状更敏感，但对量化噪声与低反射端更敏感。本实验同一组库谱分别跑 SAM 与 SID，比较纯区精度的差异并解释原因。

### 5. 线性解混与约束最小二乘

已知端元矩阵 E，解超定方程 x = E·f 的最小二乘，再施加物性约束：负丰度截断为 0、总和对一归一化（平台实现为 LS + clip[0,1] + unit-sum renorm）。重构误差 ‖x − E·f‖ 是解混质量的直接检验：误差大说明端元集合不完备或混合模型非线性。

## 实验数据

| 项目 | 说明 |
|------|------|
| 影像 | 128×128、10 波段 VNIR 合成场景（450–900 nm，FWHM 25 nm，float32，1/255 量化） |
| 地物 | 水体（NIR 深吸收）/ 植被（700→750nm 红边陡升）/ 裸土（单调上升）三端元 |
| 布局 | 左 1/3 水体、中段植被、右 1/3 裸土；两条 8 像元宽线性丰度渐变过渡带 |
| 噪声 | 每波段独立高斯噪声 σ = 0.004（约一个 1/255 量化步长） |
| 光谱库 | `data/labs/spectral-library/lab10_sicnu_library.json`（sicnu-spectral-library v1，平台原生格式，**真值源**） |
| 数据规格 | `data/labs/data-specs/lab10_hyperspectral_analysis.json`（离线生成，无需联网） |
| 本地临时数据 | `python3 scripts/gen_lab_fixtures.py hyperspectral --out data/labs/_tmp`（gitignored） |

## 实验步骤

> 判分以 headless 管道产物为准。

### 10.1 认识数据

查看 10 波段影像的逐波段波长（WAVELENGTH 元数据）；点选水体/植被/裸土纯像元看光谱曲线，对照光谱库理解三条特征谱。

### 10.2 MNF 降维（`rs:mnf`）

`numComponents=4` 运行 MNF。观察：分量 1 强对比三类地物（高 SNR），分量 4 近乎噪声。理解「有效维数」为什么 ≈ 端元数 − 1。

### 10.3 PPI 端元提取（`rs:endmember_extraction`）

`nEndmembers=3`、`projections=1000`。结果为 JSON：把三条 endmembers 与光谱库逐条对照（判分按 SAM 角均值 ≤ 15°、单条最大 ≤ 25°），确认凸包顶点 ≈ 纯端元；理解 ppiCounts 的极值计数含义。

### 10.4 SAM 与 SID 匹配分类（`rs:sam_classify`）

用库谱作 `refs` 分别以 `metric=sam` 与 `metric=sid` 分类。比较纯区精度（判分：SAM ≥ 95%，SID ≥ 90%）并解释 SID 对量化噪声更敏感的原因；`angleOut` 栅格可查每个像元的最小匹配角（注意：栅格单位为弧度，判分与文档中的角度容差为度）。

### 10.5 线性解混（`rs:spectral_unmixing`）

用库谱作 `endmembers` 解混，输出 3 个丰度波段 + 重构误差（`errorOut`）。读图：纯区自家丰度 ≥ 0.9；过渡带的丰度渐变被还原；`meanError ≤ 0.02` 说明三端元模型完备。

### 10.6 综合判读

流程图：数据 → MNF（看结构）→ PPI（找端元）→ SAM/SID（逐像元归类）→ 解混（定量占比）。讨论：若场景存在第 4 种未入库材料，解混重构误差会在哪里升高？

### Headless 运行（判分依据）

```bash
# 1) 生成本地临时数据（gitignored，实验环境由 D1 提供等价数据）
python3 scripts/gen_lab_fixtures.py hyperspectral --out data/labs/_tmp

# 2) 运行管道（先构建 build/sicnu_geo_rs_cli，见 README）
QT_QPA_PLATFORM=offscreen build/sicnu_geo_rs_cli \
  --pipeline data/labs/pipelines/lab10_hyperspectral_analysis.pipeline.json
```

成功标志：`Pipeline succeeded (5 steps)`，且 `data/labs/_tmp/out/lab10/` 下生成
`mnf_components.tif`、`sam_labels.tif`、`sam_angle.tif`、`sid_labels.tif`、
`abundances.tif`、`unmix_error.tif`（PPI 的端元 JSON 在运行输出中）。

## 预期结果

| 产物 | 预期 | 判分容差 |
|------|------|----------|
| `mnf_components.tif` | 4 分量按 SNR 有序：分量 1 类间分离度 > 分量 4 | 意图 H1 |
| PPI JSON | 3 条端元谱与库谱对照：SAM 角均值 ≤ 15°、单条最大 ≤ 25°（低反射率水体端元 + PPI 极值选择的噪声偏置），indices 互异 | 意图 H2 |
| `sam_labels.tif` | 纯区精度 ≥ 95% | 意图 H3 |
| `sid_labels.tif` | 纯区精度 ≥ 90% | 意图 H4 |
| `abundances.tif` | 纯区自家丰度 ≥ 0.9、丰度和 ≈ 1、过渡带渐变还原、meanError ≤ 0.02 | 意图 H5 |
| 管道 refs | 与光谱库逐值一致（零漂移） | 意图 H6 |

判分意图全文：`data/labs/grading/lab10_hyperspectral_analysis.intent.json`（判分器由 D4 实现）。

## 思考题

1. 为什么 MNF 分量 1 上三类地物对比强烈而分量 4 近乎噪声？「有效维数」和端元数是什么关系？
2. PPI 为什么依赖「纯像元存在」假设？如果场景里某端元只有混合像元（亚像元目标），PPI 会输出什么？
3. 同一组库谱，SAM 精度 ≥ 95% 而 SID 只有 ≥ 90%——从「幅度增益不敏感」与「概率分布敏感」两个角度解释这个差距。
4. 解混结果若出现负丰度或丰度和偏离 1，物理含义是什么？平台的 clip+renorm 处理在哪类像元上最失真？
5. 平台的 PPI 输出是 JSON 而不是栅格，且 `rs:sam_classify` 只接受内联 refs 数组——这给「管道内自动串联」带来什么困难？你会怎么设计 API？

## 术语表

| 术语 | 英文 | 释义 |
|------|------|------|
| 端元 | endmember | 混合像元分解中的「纯地物光谱」，线性混合模型的基向量 |
| 丰度 | abundance / fraction | 像元内各端元的面积/含量占比，物理约束 f_i ≥ 0 且 Σf_i = 1 |
| 光谱角 | SAM | 两条光谱在高维空间的夹角，对幅度增益不敏感的匹配度量 |
| 光谱信息散度 | SID | 把光谱归一化为概率分布后度量的 KL 散度对称量，对形状敏感 |
| 最小噪声分数 | MNF | 先噪声白化再做 PCA 的变换，分量按信噪比递减排序 |
| 像元纯度指数 | PPI | 随机单位向量投影下统计像元落入极值端次数的端元提取方法，凸包顶点像元得分最高 |

## 诚实范围（可执行子集）

- 平台无 MNF 反投影（inverse MNF）算子：在 MNF 空间提取的端元无法变回反射率空间再做 SAM/SID——本实验统一在反射率空间跑 PPI/SAM/SID/解混，MNF 仅作降维与噪声结构观察（「MNF 空间 PPI + 反投影」记入 ISSUES.md）；
- 平台无内置光谱库，`rs:sam_classify` / `rs:spectral_unmixing` 只接受内联数组（无 libraryPath 参数）；PPI 的 JSON 结果无法在管道内自动填入下一步 refs——实验以「手工从库文件复制」+ 零漂移检查（判分 H6）兜底，API 缺口记入 ISSUES.md；
- SID 要求正谱：合成数据最小值 0.001；真实数据需先行做反射率正值约束。

完整算子缺口清单见仓库 `ISSUES.md`。
