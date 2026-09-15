# TEST_MATRIX — 能力 → 独立 oracle → 命令 → exit → evidence

所有测试：`QT_QPA_PLATFORM=offscreen`，`ctest --test-dir build-dev -R <target> -j1`，
构建 `cmake --build build-dev -j2`（压力降级 -j1）。Oracle 独立性：期望值全部
来自手工推导/闭式解/独立参照实现（测试文件内注明），不复用被测实现。

| # | 能力 | 测试 target | 独立 oracle | exit | evidence |
|---|---|---|---|---|---|
| T1 | 类序 sorted/去重/JSON 往返 | test_class_order | 手工期望数组 | ☐ | ☐ |
| T2 | Platt 闭式已知解（完美可分 sigmoid 收敛、B/A 有限） | test_probability_calibration | 手推 sigmoid 参数区间 | ☐ | ☐ |
| T3 | isotonic 单调性+PAV 已知解 | test_probability_calibration | 手算阶梯解 | ☐ | ☐ |
| T4 | Brier/ECE 手算值 + bins 总数=N | test_probability_calibration | 手算样例 | ☐ | ☐ |
| T5 | entropy/margin/disagreement 手工数值（含 p=0、和≠1 输入拒绝） | test_uncertainty | 手算 | ☐ | ☐ |
| T6 | feature schema fingerprint 稳定/顺序敏感/JSON 往返 | test_feature_schema | FNV-1a 参照值 | ☐ | ☐ |
| T7 | SVM OvR decision scores 符号/可分性（opt-in 开启） | test_classifier_svm（扩展） | 线性可分几何 | ☐ | ☐ |
| T8 | NB labels 旁车往返 + 列序一致 | test_classifier_normalbayes（扩展） | 旁车文件读取对照 | ☐ | ☐ |
| T9 | pipeline uncertainty 栅格 known-answer + reject 掩码 | test_classification_pipeline（扩展） | 构造概率场手算 | ☐ | ☐ |
| T10 | sidecar v2 往返 + v1 读取兼容 | test_classification_pipeline（扩展） | 字面量 JSON | ☐ | ☐ |
| T11 | 空间泄漏被 audit 捕获（random vs spatial fold 反转） | test_spatial_cross_validation | 合成块状数据已知结论 | ☐ | ☐ |
| T12 | buffer 剔除语义（min dist > buffer） | test_spatial_cross_validation | 手算坐标 | ☐ | ☐ |
| T13 | group folds 组完整性 | test_spatial_cross_validation | 手工分组 | ☐ | ☐ |
| T14 | 对象邻接多数平滑 known-answer 图 | test_classification_object_postprocess | 手绘地图 | ☐ | ☐ |
| T15 | min-area 并边规则 + NoData 永不吸收 | test_classification_object_postprocess | 手绘地图 | ☐ | ☐ |
| T16 | RF feature importance 非负、区分性特征更高 | test_classifier_random_forest（扩展） | 合成强特征 | ☐ | ☐ |
| T17 | studio 数据准备（bins/top-k pairs）known-answer | test_classification_studio_widget（扩展） | 手算 | ☐ | ☐ |
| T18 | E2E：泄漏/不平衡/校准 Brier 下降/对象图/artifact 重放 | test_classification_intelligence_e2e | 各节内声明 | ☐ | ☐ |
| T19 | 100k 样本有界（RUN_SERIAL+TIMEOUT） | test_classification_intelligence_scale | 不变式 | ☐ | ☐ |
| T20 | 回归：既有分类 suites 全绿 | test_classifier_* 全家族 | master 基线行为 | ☐ | ☐ |

Oracle 锚点（GOAL Loop Oracle 映射）：O1→T11；O2→T1/T5/T8；O3→T10/T18(e2e replay)；
O4→T20+全家族两遍；O5→diff check；O6→Phase 8 双跑记录；O7→REVIEW_LOG。
