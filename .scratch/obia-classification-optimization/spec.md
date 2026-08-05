# OBIA Classification Engine Optimization & UI Feature Tree Specification

## Problem Statement

Users performing Object-Based Image Analysis (OBIA) in remote sensing workflows require high-precision, flexible segment classification, active learning candidate visualization, and multi-scale hierarchy consolidation.

Currently:
1. Active learning entropy candidate tables (`mUncertaintyTable`) are instantiated but not populated with computed segment prediction uncertainties upon classification completion.
2. Advanced classifier backends (such as Neural Network MLP) and hyperparameter controls (e.g., Random Forest `minSampleCount`) need full backend factory and GUI dialog wiring.
3. Feature scaling lacks MinMax scaling options, and hierarchy class consolidation needs probability-weighted voting options alongside majority vote and top-down inheritance.
4. Code review standards require strict adherence to 2-space Qt6/C++17 indentation and removal of speculative prediction fallbacks.

## Solution

Build a robust, end-to-end OBIA classification enhancement suite with:
1. Active Learning Candidate Table Integration: Populate `mUncertaintyTable` with high-entropy segment predictions, enabling double-click canvas centering and rapid ROI labeling.
2. Complete Classifier Backend Expansion: Register `RsMlpBackend` (`cv::ml::ANN_MLP`) in `RsClassifierBackendFactory` and expose complete hyperparameter controls (`numTrees`, `maxDepth`, `minSampleCount`).
3. Multi-Strategy Feature Scaler & Hierarchy Consolidator: Support both Z-score and MinMax feature scaling, and provide unweighted majority voting, top-down inheritance, and probability-weighted voting in hierarchy consolidation.
4. Hierarchical Feature Selection Tree: Allow users to interactively check/uncheck spectral, GLCM texture, and shape descriptor categories via a dedicated `QTreeWidget` dock before classification execution.
5. Code Health & Standards Alignment: Enforce 2-space Qt6/C++17 formatting across all analysis headers/sources and replace speculative prediction heuristics with strict boundary checks.

## User Stories

1. As a remote sensing analyst, I want to view a ranked list of high-uncertainty (high-entropy) segments after classification, so that I can quickly perform active learning ROI annotation on ambiguous segments.
2. As a remote sensing analyst, I want to double-click an uncertainty candidate row, so that the map canvas automatically centers and highlights the corresponding segment for fast labeling.
3. As a GIS specialist, I want to select between Random Forest, Normal Bayes, SVM, and Neural Network (MLP) classifiers, so that I can apply the optimal machine learning algorithm for my specific imagery.
4. As a machine learning practitioner, I want to tune Random Forest hyperparameters (`numTrees`, `maxDepth`, `minSampleCount`) from a GUI dialog, so that I can prevent model overfitting or underfitting.
5. As a remote sensing researcher, I want an interactive Feature Selection Tree panel, so that I can enable or disable spectral, GLCM texture, and geometric shape feature groups prior to model training.
6. As a GIS user, I want feature statistics to be normalized using Z-score or MinMax scaling before model training, so that features with large numerical ranges do not dominate distance/probability metrics.
7. As an analyst working with multi-scale segment hierarchies, I want to run hierarchy class consolidation using majority vote, top-down inheritance, or probability-weighted voting, so that fine and coarse segment labels remain logically consistent across levels.
8. As a software developer, I want all C++ analysis classes to follow strict 2-space indentation and clean error-handling contracts, so that the codebase remains readable, maintainable, and aligned with repository standards.

## Implementation Decisions

### Modules & Interfaces
- **Classifier Backend Engine**:
  - `RsClassifierBackendFactory`: Register `"RandomForest"`, `"NormalBayes"`, `"SVM"`, and `"MLP"`.
  - `RsRandomForestBackend`: Accept `(numTrees, maxDepth, minSampleCount)` and compute prediction probabilities without speculative fallback heuristics.
  - `RsMlpBackend`: Wrap OpenCV `cv::ml::ANN_MLP` with network layer configuration and probability normalization.

- **Feature Matrix & Selection**:
  - `RsFeatureSelection`: Struct encapsulating feature group toggles (`useMean`, `useStdDev`, `useMin`, `useMax`, `useGlcmContrast`, `useGlcmCorrelation`, `useGlcmEnergy`, `useGlcmHomogeneity`, `useArea`, `usePerimeter`, `useShapeIndex`, `useCompactness`, `useRectangularity`, `useAspectRatio`).
  - `RsSegmentFeatures::toFeatureMatrix`: Build column-filtered `cv::Mat` matching active `RsFeatureSelection` flags.

- **Active Learning & Uncertainty Visualization**:
  - `RsObjectClassifyResult`: Store `segmentUncertainties` (`QMap<quint32, double>`) computed via Shannon entropy $H = -\sum p_i \log_2 p_i$.
  - `RsObiaMainWindow`: Populate `mUncertaintyTable` sorted descending by entropy upon task completion, showing `Seg ID`, `Entropy (H)`, and `Predicted Class`.

- **Hierarchy Class Consolidator**:
  - `RsHierarchyClassConsolidator`: Support `BottomUpMajorityVote`, `TopDownInheritance`, and `ProbabilityWeightedVote`.

## Testing Decisions

### Good Test Principles
- Test external contracts and expected mathematical properties rather than internal private state.
- Ensure 100% Catch2 automated unit test coverage across all classifier backends, feature selection masks, active learning uncertainty calculations, and hierarchy consolidators.

### Modules to Test
- `test_classifier_random_forest`: Verify RF model fitting, class predictions, and probability distributions.
- `test_feature_selection`: Verify selective matrix construction and column filtering given custom `RsFeatureSelection` masks.
- `test_hierarchy_class_consolidator`: Verify multi-level class propagation under Bottom-Up, Top-Down, and Weighted strategies.
- `test_obia_segmentation` & `test_obia_task`: Verify full end-to-end task execution and uncertainty dictionary populating.

### Prior Art
- Existing Catch2 test targets in `tests/CMakeLists.txt` using `sicnu_discover_tests`.

## Out of Scope

- GPU-accelerated XGBoost / LightGBM native C++ bindings.
- Deep learning end-to-end pixel segmentation (U-Net / SegNet ONNX runtime fine-tuning).

## Further Notes

- All C++ source and header files must strictly follow 2-space indentation.
