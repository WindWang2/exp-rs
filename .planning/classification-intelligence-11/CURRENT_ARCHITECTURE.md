# CURRENT_ARCHITECTURE — 分类域 authority/seam 图（基线 a5b11b7f10）

## 层次与 authority

```
┌ GUI: src/app/classification/*（3956 行主窗 qgsclassificationmainwindow，
│      accuracy panel, post-process dialog…）        [本 track 只读]
│      src/app/workbench/classification_studio_widget.*（D15 studio shell）
│      [本 track WP-F 唯一 UI 写点]
├ Operators: src/operators/rs/
│      rs_supervised_classification_operator（像素监督，probabilityOutput 白名单
│        normal_bayes/rf/mlp）                        [本 track additive]
│      rs_obia_classify / rs_obia_segment / rs_obia_features / rs_segment_stats
│      rs_majority_filter / rs_sieve（消费 D15 postprocess）
│      rs_post_classification_change（转移矩阵）
├ Processing: src/processing/algorithms/
│      classifier_engine.*（rs::processing 自研 5 核：KMeans/ISODATA/RF/
│        RBF-SVM/NormalBayes；概率契约=升序训练标签，RF/NB 真后验，
│        其余 1-hot；与 OpenCV 栈互不依赖）           [本 track 只读]
│      classification_postprocess.*（D15 像素 majority/sieve/clump，
│        NoData=-1 永不吸收）                          [本 track 只读]
│      glcm_texture.*（D15 纯 STL GLCM，NaN 窗口透传） [消费]
│      classification_object_postprocess.*（★本 track 新增）
├ Analysis: src/analysis/classification/
│      RsClassifierBackend（fit/predict/predictProbabilities/
│        supportsProbabilities/needsLabelRemap 抽象）
│      ├─ RsRandomForestBackend（RTrees 树投票真概率 + labels.json）
│      ├─ RsMlpBackend（softmax + labels.json + NaN 探针）
│      ├─ RsNormalBayesBackend（高斯后验逐行归一化；★类序未持久化=缺口）
│      ├─ RsSvmBackend（C_SVC+RBF, C=10/γ=0.5；无概率）★opt-in OvR 计划
│      ├─ kNN / KMeans / ISODATA / Mahalanobis / MinDistance / Statistical
│      RsClassifierBackendFactory（子串匹配；RsClassifierBackendParams）
│      RsClassificationPipeline（train→sidecar→tiled predict 256×256→
│        GTiff 写出；NoData/ignore→unclassified；dtype 升级；
│        probabilityOutput(ADR 0094)；.meta.json superset sidecar v1）
│      RsCrossValidation（分层 k-fold）、RsClassificationSplit（分层+groupIds）
│      RsFeatureScaler（ZScore/MinMax, JSON v1；★NaN 无防御）
│      RsAccuracyAssessment（混淆/OA/kappa/P/R/F1）
│      RsTrainingDataExtraction（矢量化+RasterIO→X；maxSamplesPerClass mt19937）
│      ★本 track 新增：rs_class_order / rs_probability_calibration /
│        rs_uncertainty / rs_feature_schema / rs_spatial_cross_validation
├ Segmentation: src/analysis/segmentation/（segmenters, RsSegmentFeatures
│      光谱+GLCM+shape, RsObjectClassify 逐对象, hierarchy consolidator）
│                                                    [本 track 只读]
└ Core: src/core/spatial_split.*（D15 块划分+guard buffer+Moran's I；
       隔离不变量 ||p_train-p_eval||>buffer；无生产消费者）[只读，语义引用]
```

## 旁车/artifact authority

- 模型本体：OpenCV YAML（cv::Algorithm save/load）。
- `<model>.labels.json`：RF/MLP 类列序（{version, classIds[]}）。
- `<model>.meta.json`：pipeline superset（v1：method/scaler/classes/
  features=1-based 波段号/validation/kmeansRemap）→ ★本 track 升 v2。
- `.rscproj`：GUI 工程快照（本 track 不动）。
- DL scene 模型：exp-rs-classification/1（rs_model_task_operators，本 track 不动）。

## 已识别的权威原则（沿用）

1. 类列序 = 升序去重训练标签（RF/MLP 现行）；本 track 把它升为全局契约。
2. NoData 语义：像素管线 unclassified=0 / ignore 选项；D15 后处理 sentinel=-1；
   对象后处理沿用 -1；不确定性的 rejected mask 独立波段（0/1），不改标签语义。
3. 确定性 = std::mt19937(seed=42 默认)，seed 显式传递并持久化（本 track 起
   sidecar 记录）。
4. 错误 = typed Error 枚举 + errorMessage；fail-closed（#1000 精神）。
5. 输出写出 = 原子性（部分失败清理）、dtype 升级不 clamp、cancel 删部分产物。

## 与并行 track 的边界

- 特征 kernel（spectral indices 等）归属 #1008/既有 seam；本 track 只做
  schema/组装/指纹层（DECISIONS D-012）。
- dataset 泄漏审计/划分方法学归属 D19（src/dataset/**）；本 track 的
  spatial CV 是分类矩阵域的独立评估路径（D-009）。
