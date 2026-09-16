# PLAN — Classification & Object Intelligence 11.0

基线 `origin/master = a5b11b7f10`。所有新代码落在 primary write scope
（`src/analysis/classification/**`、`src/processing/algorithms/*classif*`、
`src/app/workbench/classification_studio_widget.*`、`src/operators/rs/*classif*`、
`tests/*classif*`、`docs/processing/classification*`）。
**不改** D19 dataset 域、D18 mission 文件、PR #1008 触及的文件（尤其
`spectral_indices.*`）、`docs/adr/`（避免与 #1008 的 0158 号段竞争，设计记录放
`docs/processing/classification-intelligence.md` + DECISIONS.md）。

## Phase 1 — 契约/数据模型（对应 WP-A schema 层 + WP-B 接口 + WP-G sidecar 契约）

新文件（src/analysis/classification/）：
1. `rs_class_order.h/.cpp` — 概率列序唯一权威：
   `sortedClassIds(y)` → 升序去重；`classOrderToJson/fromJson`；`validateColumns(K)`。
   这是 Oracle 2（class order 稳定）的机器可验证落点。
2. `rs_probability_calibration.h/.cpp` — `RsProbabilityCalibrator`：
   Platt（1-D sigmoid A,B，确定性 Newton 迭代）+ isotonic（PAV）；多类 one-vs-rest；
   输入 = decision score 或已有概率；`toJson/fromJson`（artifact 持久化）。
   `RsCalibrationMetrics::compute(yTrue, probs, classIds, bins)` → 多类 Brier、
   置信 ECE、reliability bins（N、meanConfidence、empiricalAccuracy per bin）。
3. `rs_feature_schema.h/.cpp` — `RsFeatureSchema`：有序 (name, kind, source) 列表
   + `fingerprint()`（FNV-1a 64 of names+order+schema version，跨平台稳定）；
   `RsFeatureAssembler`：把命名特征列组装为 X 矩阵，missing/NoData 策略显式
   （NaN 列 → 文档化 fail-closed 选项或按列有效掩码）。
4. `rs_uncertainty.h/.cpp` — `RsUncertainty`：entropy（log2，归一化和=1）、margin
   （p1-p2）、ensemble disagreement（平均逐类方差和 / 或交叉熵散度，固定定义）；
   `RsRejectOption`：阈值语义（measure >= threshold → rejected）。
5. `rs_feature_scaler.*` NaN/NoData 防御（Phase 5 硬化，此处仅定契约：fit 遇
   非有限值 → 返回 false + error，不静默污染）。
6. NormalBayes `.labels.json` 类序旁车（对齐 RF/MLP 现有模式）。
7. 单测：`test_class_order.cpp`、`test_probability_calibration.cpp`（闭式
   known-answer：完美分数、过度自信、随机标签）、`test_feature_schema.cpp`、
   `test_uncertainty.cpp`（手工可验数值）。

## Phase 2 — 核心算法一大块（WP-B 校准落地 + WP-D 图级集成）

1. `RsClassifierBackend::decisionScores()` additive virtual（默认空）；
   SVM 实现：fit 时可选 `enableOvRDecisionScores`（默认 false，零行为变化）
   训练 K 个 one-vs-rest 二类 SVM，`decisionScores` 返回 NxK margin；
   `decisionScoresToProbabilities`（sigmoid per class，由校准器负责拟合参数）。
2. `RsClassificationPipeline` additive Config：`uncertaintyOutput`（Float32
   多波段：entropy+margin+rejected mask）、`uncertaintyMeasure`、
   `rejectThreshold`（<0 = off，默认 off → 零行为变化）、`applyCalibration`
   + sidecar calibration 节消费。
3. sidecar version=2（additive keys：`classOrder`、`calibration`、`uncertainty`、
   `training{seed,hyperparams,trainSamples}`、`featureSchema{name,fingerprint}`）；
   loader 兼容 v1/v2；写 v2。
4. 单测：SVM OvR margin known-answer（线性可分 margin 符号正确）；pipeline
   uncertainty 栅格 known-answer；sidecar v2 往返 + v1 读取。

## Phase 3 — 核心算法二大块（WP-C spatial CV 2.0 + WP-E 对象级后处理）

1. `rs_spatial_cross_validation.h/.cpp`（analysis 层）：
   - fold 生成：`blockFolds(coords, gridKxK)`、`groupFolds(groupIds, k)`、
     `bufferedBlockFolds(coords, bufferDist)`（train/test 最小距离 < buffer 的
     test 样本剔除 = ExcludedBuffer，语义对齐 core/spatial_split.h 的隔离不变量）；
   - `LeakageAudit`：per fold 记录 min train-test 距离、组重叠计数；
     随机 fold 在空间聚集数据上 min-dist=0 → audit 报告 `SpatialOverlap`；
   - `evaluate(X, y, coords/groups, factory, k)` → per-fold accuracy + audit。
     纯矩阵+坐标，**零 D19 dataset 依赖**。
2. `classification_object_postprocess.h/.cpp`（processing 层，与 D15
   classification_postprocess 并列新文件）：
   - segment adjacency 从 (label raster, segmentId raster) 构建（4/8 连接）；
   - per-segment 多数类投票（tie → 较小 class id，与 rs_majority_vote 一致）；
   - 迭代邻接多数平滑（strict-majority 语义，不满足则保持）；min-area 规则
     （小于阈值的 segment 并入邻接最长公共边 segment）；
   - NoData segment（id<=0 / -1）永不吸收。
3. 单测：`test_spatial_cross_validation.cpp`（合成空间泄漏：块状聚集数据上
   random k-fold 高精度 vs spatial fold 低精度，audit 必须 flag —— Oracle 1）、
   `test_classification_object_postprocess.cpp`（known-answer 地图）。

## Phase 4 — surface/integration（WP-F studio 11 + operator surface）

1. `classification_studio_widget.*` 内新增（仅自有文件）：
   - `ProbabilityPanel`：类概率条形（class order 稳定）+ 熵/margin 数值；
   - `ReliabilityWidget`：reliability bins 渲染（校准前后对比数据由调用方喂）；
   - `ConfusionPairWidget`：混淆 top-k 类对（来自 RsAccuracyAssessment::Result）；
   - `FeatureImportanceWidget`：条形渲染（数据经 additive
     `RsClassifierBackend::featureImportances()`，RF 用 RTrees 原生）；
   - 纯数据准备函数（bins/top-k pairs）与绘制分离，headless 可测。
2. `rs:supervised_classification` 算子 additive 参数：`uncertaintyOutput`、
   `uncertaintyMeasure`、`rejectThreshold`、`applyCalibration`；schema 同步。
3. `docs/processing/classification.md`（新）：概率/类序/校准/不确定性/空间 CV/
   对象后处理契约文档（不建 ADR，避免 #1008 号段冲突）。
4. `test_classification_studio_widget.cpp` 扩展：数据准备函数 known-answer。

## Phase 5 — 硬化

1. Scaler NaN 防御实现 + 负测试；Unicode path 用 QFileInfo 现有模式核查；
2. 新循环 cancel 检查点（fold 间/对象间/平滑迭代间）；对象图构建内存上限
   （segment 数上限参数，超出 → typed refusal）；
3. `test_classification_intelligence_scale.cpp`：100k 样本 × 4 特征
   NormalBayes train+predict 有界（RUN_SERIAL、TIMEOUT、断言结果不变式而非
   wall-clock）；校准拟合 100k 分数线性时间；
4. factory 子串误命中 / SVM C-γ 硬编码 → disposition 记录（follow-up，
   不在改行为路径上）。

## Phase 6 — E2E/known-answer

`test_classification_intelligence_e2e.cpp`：
1. synthetic spatial leak 端到端（生成→random CV→spatial CV→audit flag）；
2. imbalanced classes（3 类 1:10:50）准确率与拒绝语义；
3. calibration known-answer（过度自信 logits → Platt 后 Brier 下降断言）；
4. object map（label+segment rasters → 平滑+min-area known-answer 图）；
5. artifact replay（seed 固定 → train/save/load/predict 逐样本一致）。

## Phase 7 — adversarial review

Subagent #2 只读全 diff review（architecture/science/concurrency/compat/
test-oracle/security/UI/docs 八轴）→ REVIEW_LOG.md → P0/P1 全修 → targeted gate 重跑。

## Phase 8 — 双验证 + PR

Oracle 4/5/6 双跑；rebase origin/master；push；`gh pr create`；不 merge。

## 验证矩阵入口

见 TEST_MATRIX.md。构建：`cmake --build build-dev -j2`（压力高降 -j1）；
测试：`QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -R <family> -j1`。
