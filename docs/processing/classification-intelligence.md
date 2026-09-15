# Classification Intelligence 11.0 — 契约文档

本文件是 F12 track（概率分类、特征管线、空间验证、不确定性、对象级后处理）
的**契约真值**。所有新增 seam 的语义、单位、方向、失败行为以此为准；
与代码注释冲突时，先修文档或代码使其一致，不允许长期分叉。

---

## 1. 类别列序（RsClassOrder）— 概率的唯一坐标系统

**定义**：任何 `N×K` 类概率/决策分数矩阵中，第 k 列指代
`classIds[k]`，其中 `classIds` 为**严格升序去重**的训练标签列表。

**序列化**：裸 JSON 数组（`[1,3,7]`），与 RF/MLP 既有
`<model>.labels.json` 旁车格式完全一致。解析失败（非数组、非整数、
非严格升序）→ 拒绝。

**产出方义务**：RF/MLP/NormalBayes（fit 时捕获）、SVM OvR（fit 时捕获）、
pipeline `.meta.json` v2 `classOrder` 节。 NormalBayes 旁车写失败 =
save 失败（fail-closed；RF/MLP 维持历史 best-effort 行为不变）。
遗留无旁车模型仍可加载，但 `classOrder()` 为空，消费方不得伪造列序。

## 2. 概率与决策分数

| 后端 | 概率 | 语义 |
|---|---|---|
| RandomForest | ✔ | 树投票频率归一化（既有） |
| NormalBayes | ✔ | 高斯后验逐行归一化（既有） |
| MLP | ✔ | softmax（既有） |
| SVM | decision scores（opt-in） | one-vs-rest margin 距离，**不是概率** |
| kNN / Mahalanobis / MinDistance | ✘ | — |

**SVM OvR（D-005）**：构造 `RsClassifierSvm(/*enableOvRDecisionScores=*/true)`
时，`fit()` 额外训练 K 个二类 C_SVC；`decisionScores(X)` 返回 `N×K`
CV_32F margin，列序 = `classOrder()`。RAW_OUTPUT 符号在 fit 时用训练
正样本校准（`mOvrSigns`），保证"越大越可能属于该类"全后端一致。
OvR 模型持久化为 `<model>.ovr.json` + `<model>.ovr<i>.yaml`，写失败 =
save 失败（OvR 模型丢 ensemble 必须失败，不得静默降级）。

margin → 概率必须经校准器（下节）；禁止在调用方手工 sigmoid 后当作校准
概率使用而不落盘校准参数。

## 3. 概率校准（RsProbabilityCalibrator）

- **拟合在分类器之外**：输入 = 留出校准集的 raw scores/probabilities
  （`N×K`）+ 真标签；不改变任何后端训练行为。
- Platt：逐类 1-D 逻辑回归 `p = 1/(1+exp(A·s+B))`，Newton 迭代
  （默认 maxIter=100），Platt(1999) 热启动；梯度范数 < 1e-10 收敛；
  Hessian 奇异加 ridge。
- isotonic：逐类 PAV（池聚 violator），应用 = knot 中点间单调分段线性
  插值，端点截断。
- **多类 = one-vs-rest**；`apply()` 逐行归一化到和 1；isotonic 可能整行
  映 0 → 文档化回退 = 均匀分布 1/K。
- **fail-closed**：尺寸不匹配 / 非有限值 / 某类无正或无负样本 → fit 返回
  false，模型保持无效。
- 持久化：`RsCalibrationModel::toJson/fromJson`
  （`{version:1, method:"platt"|"isotonic", classIds:[…], platt|isotonic}`），
  嵌入 sidecar v2 `calibration` 节。

### 可靠性度量（RsCalibrationMetrics）

- `brier`：多类 Brier，`mean_i Σ_k (p_ik − 1{k=y_i})²`。
- `ece`：top-class 置信 ECE，`Σ_bin (n_bin/N)·|acc_bin − conf_bin|`，
  等宽 bins（默认 10，上限 1000）。
- `logLoss`：`mean −ln p_true`（p 截断 1e-12）。
- 输入行和必须 ∈ 1±1e-2，否则 fail-closed。

## 4. 不确定性（RsUncertainty）— 定义被测试锁定

| 度量 | 定义 | 值域 | reject 方向 |
|---|---|---|---|
| entropy | `−Σ p·log2(p)`，0·log0:=0 | `[0, log2 K]`（图输出为**归一化** `[0,1]`） | `H ≥ t` |
| margin | 降序前二差 `p1−p2` | `[0,1]` | `M ≤ t` |
| confidence | `max_k p_k` | `[0,1]` | `C ≤ t` |
| disagreement | 逐类总体方差按类平均 | `[0, 0.25]`（K=2 时） | —（无逐像素图） |

输入行校验：非负、有限、和 ∈ 1±1e-3，否则 fail-closed。

## 5. Pipeline 不确定性与拒绝（全 additive，默认关）

`RsClassificationPipeline::Config` 新字段：

- `uncertaintyOutput`：3 波段 Float32 GTiff
  （band 1 = 归一化熵，band 2 = margin，band 3 = rejected mask {0,1}；
  波段描述符 `normalised_entropy` / `margin` / `rejected_mask`；
  NoData = −1 于 ignored 像素）。
- `uncertaintyMeasure`：mask 驱动度量（默认 entropy）。
- `rejectThreshold < 0` = 关（默认）；mask 全 0。
- **标签图永不因 rejection 改写**（D-006）；拒绝语义只在 mask 波段。
- `probabilityOutput` / `uncertaintyOutput` 都要求
  `supportsProbabilities()` 后端（normal_bayes / rf / mlp）。
- `calibrationModel`（显式）或 predict-only 模式 +
  `applySidecarCalibration=true` 时消费 sidecar `calibration` 节。
  **校准只影响 confidence/uncertainty 统计与概率栅格，不重判硬标签**；
  K 不匹配 = 带类型错误失败。
- 原子性沿既有模式：临时文件 + 成功后 rename，任一失败清理全部部分产物。

## 6. 模型旁车（.meta.json）v2

- 写 v2；读 v1+v2。v1 文件 = F12 节缺省。
- 新节（全部可选，缺省不写）：`classOrder`、`calibration`、
  `featureSchema`、`training{seed,trainSamples}`。
- `features` 节仍是 1-based 波段号（v1 语义不变）；命名特征 schema 在
  `featureSchema`（下节）。

## 7. 特征 schema 与指纹（RsFeatureSchema）

- 列 = 有序 `(name, kind∈{band,index,texture,terrain,temporal,other}, source)`；
  重名/空名拒绝。
- `fingerprint()` = FNV-1a 64（offset 14695981039346656037, prime
  1099511628211）over `"exp-rs-feature-schema/1"` + 每 descriptor
  `"|name|kind|source"`；16 位小写 hex。**列顺序是语义的一部分**，
  顺序变 = 指纹变。
- `fromJson` 校验文档内记录的指纹与 descriptors 重算一致（漂移门）。
- 组装器（`RsFeatureAssembler`）：NoData 标记值 → 输出 NaN 哨兵 +
  逐列 valid counts；非有限输入同样归 NaN。**NaN 跨 seam 是唯一
  missing 表示**；`RsFeatureScaler` 在 fit 遇非有限训练输入时
  fail-closed（见 §10）。
- 特征**值**仍由既有域 kernel 产生（spectral indices / glcm_texture /
  terrain / temporal）；本层不重复实现任何 kernel。

## 8. 空间交叉验证（RsSpatialCrossValidation）

- fold 三型：
  - `groupFolds`：组原子（GroupKFold balance-first 划分，desc 大小 /
    asc id tie）；
  - `blockFolds`：坐标包围盒 nx×ny 网格，每非空块一折；
  - `bufferedBlockFolds`：块折 + 训练剔除（距该折任一 test 样本
    `≤ bufferDistance` 的训练样本 → `excludedBuffer`）。隔离不变量与
    `src/core/spatial_split.h` 同源：保留的 train/test 对
    `||p_train − p_eval|| > bufferDistance`。
  - `randomFolds`：类内 round-robin（对照基线）。
- **审计（Oracle 1）**：逐折记录保留 train/test 最小欧氏距离（坐标单位）
  与组重叠计数；`spatiallyClean()` = 全折 minDist > 0 且零组重叠。
  测试固定证明：块状泄漏数据上 random folds 高精度 + audit 告警，
  spatial folds 低精度 + clean。
- 与 D19 dataset 泄漏审计/划分方法学**完全解耦**（纯矩阵+坐标）；
  与 `src/core/spatial_split.h` 是不同 API 面（k-fold 评估 vs 三分划分）。

## 9. 对象级后处理（ClassificationObjectPostProcessor）

- 输入：类标签栅格 + 段 id 栅格（同尺寸 row-major）。NoData 标签 = −1，
  NoData 段 id ≤ 0；**永不吸收/合并**。
- 逐段多数类（tie → 最小 class id，与 rs_majority_vote 一致）；
  面积 = 段像素数（含 NoData 标签像素）。
- 邻接图：4/8 连接，边权 = 共享边界像素数。
- min-area：段面积 < 阈值 → 并入共享边界最长的邻段（tie → 最小 class id，
  再最小段 id）；union-find 级联 + 有序 offender 集合（确定性）；
  孤立小段（仅 NoData 邻域）保留。
- 平滑：Jacobi 迭代严格多数（类边界权 > 一半才改；tie 保持）。
- 段数 > `maxSegments`（默认 2e6）→ typed refusal（SizeMismatch /
  TooManySegments 错误码）。

## 10. RsFeatureScaler 防御（F12 硬化）

- `fit()`：任何非有限训练值 → 返回 false（fail-closed），状态复位。
- `transform()`：非有限值传播为 NaN（位置保持），不再进入未定义均值。

## 11. 算子面（rs:supervised_classification）

新增参数（全可选）：`uncertaintyOutput`（路径）、
`uncertaintyMeasure`（entropy|margin|confidence，默认 entropy）、
`rejectThreshold`（<0 关，默认 −1）。概率能力校验与 `probabilityOutput`
一致（normal_bayes/rf/mlp）。结果回显 `uncertaintyOutput`。

## 12. Studio 11 面板

`src/app/workbench/classification_studio_widget.*` 内四个纯数据面板
（`RsProbabilityPanel` / `RsReliabilityWidget` / `RsConfusionPairsWidget` /
`RsFeatureImportanceWidget`）+ 置信摘要行。接口只收 POD 向量
（无 OpenCV/分析层类型）；畸形输入（长度不匹配、非有限、负值）被过滤，
绘制不抛异常。数据源由宿主喂入（如 RF `featureImportances()`、
`RsCalibrationMetrics::Report` bins、混淆矩阵 top-k 对）。
