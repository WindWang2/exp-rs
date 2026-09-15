# CAPABILITY_MATRIX — before → after

图例：✔=implemented（本地证据），◐=degraded/部分，✘=not supported。
before = master a5b11b7f10；after = 本 track PR。

| 能力 | before | after | 证据/说明 |
|---|---|---|---|
| RF 概率（树投票） | ✔ | ✔ | 既有（rs_classifier_random_forest.cpp） |
| NormalBayes 概率 | ✔ | ✔ | 既有 |
| NormalBayes 类序持久化 | ✘ | ✔ | rs_class_order + NB labels.json（Phase 1） |
| MLP 概率 | ✔ | ✔ | 既有（softmax） |
| SVM 概率/decision score | ✘ | ✔（opt-in） | OvR decision scores，flag 默认关（D-005） |
| 概率列序契约（统一） | ✘ | ✔ | rs_class_order.h + 全产出方测试锁定（Oracle 2） |
| Platt 校准 | ✘ | ✔ | RsProbabilityCalibrator（多类 OvR） |
| isotonic 校准 | ✘ | ✔ | PAV |
| Brier / ECE / reliability bins | ✘ | ✔ | RsCalibrationMetrics |
| 校准持久化+重放 | ✘ | ✔ | sidecar v2 calibration 节 |
| 像素 entropy 图 | ✘ | ✔ | pipeline uncertaintyOutput 波段1 |
| 像素 margin 图 | ✘ | ✔ | 波段2 |
| reject/abstain 选项 | ✘ | ✔ | rejectThreshold（默认 off，D-006/D-007） |
| ensemble disagreement | ✘ | ✔ | rs_uncertainty（模型列表级） |
| group k-fold CV | ◐（split 有 group，CV 无） | ✔ | rs_spatial_cross_validation |
| block spatial k-fold | ✘ | ✔ | 坐标网格 folds |
| buffered spatial folds（隔离不变量） | ✘（仅 D15 三分 seam 无生产） | ✔ | 与 core/spatial_split 同源不变量（D-009） |
| CV 泄漏 audit（min dist/组重叠） | ✘ | ✔ | LeakageAudit；合成泄漏测试捕获（Oracle 1） |
| 训练特征 typed schema/名称 | ✘ | ✔ | RsFeatureSchema（D-012 深度） |
| 特征 fingerprint 持久化 | ✘ | ✔ | sidecar v2 featureSchema |
| Scaler NaN/NoData 防御 | ✘ | ✔ | fit fail-closed（Phase 5） |
| 对象邻接图 | ✘ | ✔ | classification_object_postprocess |
| 对象级 smoothing | ✘ | ✔ | 邻接多数平滑（strict majority） |
| min-area 对象规则 | ◐（像素 sieve） | ✔ | segment 面积阈值并边规则 |
| Studio 概率/置信视图 | ✘ | ✔ | ProbabilityPanel |
| Studio reliability 视图 | ✘ | ✔ | ReliabilityWidget |
| Studio 混淆 pair 视图 | ✘ | ✔ | ConfusionPairWidget |
| Studio feature importance | ✘ | ✔ | RF importance + FeatureImportanceWidget |
| sidecar seed/超参/样本数 | ✘ | ✔ | sidecar v2 training 节 |
| artifact 确定性重放 | ◐（模型 YAML 往返） | ✔ | e2e replay 测试（Oracle 3） |
| 100k 样本有界证据 | ✘ | ✔ | test_classification_intelligence_scale |
| rs:supervised_classification 不确定性面 | ✘ | ✔ | additive 参数 |
| 分类 agent 工具/CV 算子 | ✘ | ✘（follow-up） | surface 决策见 PR_BODY known limitations |
| OBIA 概率栅格输出 | ✘ | ✘（follow-up） | 与 obia 内部实现耦合，另行 track |
| hierarchy ProbabilityWeightedVote 概率源 | ✘ | ✘（follow-up） | 需要 segment 概率列上游 |
