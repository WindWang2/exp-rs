# Wayfinder Map: 面向对象分类进一步优化 (OBIA Classification Optimization)

## Destination

构建高精度、多分类算法支持、带特征标准化与主动学习的面向对象分类引擎：新增 Random Forest / MLP 分类器后端，实现 Z-score/MinMax 特征标准化与重要性筛选，提供预测不确定性采样（Uncertainty Sampling）与多尺度层次分类一致性约束。

## Notes

- **Domain**: Remote Sensing Object-Based Classification Optimization (Classifier Backends, Feature Scaling/Selection, Active Learning, Multi-Scale Hierarchy Fusion).
- **Relevant Skills**: `/wayfinder`, `/codebase-design`, `/domain-modeling`, `/tdd`.
- **Architectural Seams**:
  - `RsClassifierBackend`, `RsSvmBackend`, `RsNormalBayesBackend` (`src/analysis/classification/`)
  - `RsSegmentFeatures`, `RsObjectClassify` (`src/analysis/segmentation/`)
  - `RsObiaMainWindow`, `RsAccuracyAssessment` (`src/app/obia/`)

## Decisions so far

- [01 — Random Forest & MLP Classifier Backend Expansion](issues/01-random-forest-and-mlp-classifier-backends.md) — Implemented OpenCV `RTrees` backend with probability predictions and UI integration.
- [02 — Feature Scaling & Importance Selection Engine](issues/02-feature-scaling-and-importance-selection.md) — Integrated `RsFeatureScaler` Z-score standardization into `RsObjectClassify::classify`.
- [03 — Active Learning & Uncertainty Sampling Guided Annotation](issues/03-active-learning-uncertainty-sampling.md) — Computed Shannon entropy $H$ for prediction uncertainty and added Active Learning candidate selection dock.
- [04 — Hierarchical Multi-Scale Classification Consistency Consolidation](issues/04-hierarchical-multi-scale-class-consistency.md) — Implemented `RsHierarchyClassConsolidator` for Bottom-Up majority voting & Top-Down class inheritance.
- [05 — Hierarchical Feature Tree Selection Panel & Selective Feature Matrix Builder](issues/05-feature-selection-tree-panel.md) — Exposed hierarchical Feature Tree (`QTreeWidget` dock) and integrated `RsFeatureSelection` mask into `RsSegmentFeatures::toFeatureMatrix` and `RsObiaTask`.
- [06 — Standards & Code Quality Cleanup](issues/06-standards-and-code-quality-cleanup.md) — Standardized 2-space Qt6/C++17 indentation across analysis headers/sources and replaced speculative prediction heuristics with strict boundary clamps.
- [07 — Active Learning Candidate Table Population](issues/07-active-learning-uncertainty-table-population.md) — Connected computed prediction entropy candidates to `mUncertaintyTable` in `RsObiaMainWindow`, enabling double-click canvas centering and fast ROI labeling.
- [08 — Neural Network (MLP) Backend & Hyperparameter Dialog Expansion](issues/08-mlp-backend-and-hyperparameter-dialog-expansion.md) — Implemented `RsMlpBackend` (`cv::ml::ANN_MLP`), registered in `RsClassifierBackendFactory`, and added `minSampleCount` GUI tuning control.
- [09 — MinMax Feature Normalization & Probability-Weighted Hierarchy Consolidation](issues/09-minmax-scaling-and-weighted-hierarchy-consolidation.md) — Implemented MinMax scaling mode in `RsFeatureScaler` and `ProbabilityWeightedVote` area-weighted voting in `RsHierarchyClassConsolidator`.

## Not yet specified

- **GPU-Accelerated XGBoost/LightGBM Native Integration**: 外部 C API 动态链接库与 GPU 显存级数据传递。
- **Semi-Supervised Label Propagation**: 基于图论（Graph-based Label Propagation）的极少量样本自我伪标记。

## Out of scope

- 全图像素级端到端 Semantic Segmentation 深度学习网络微调（如 U-Net / SegNet 权重训练）。
- 实时 60fps 动态摄像头流在线面向对象分类。
