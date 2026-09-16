# TEST_MATRIX — 能力 → 独立 oracle → 命令 → exit → evidence

所有测试：`QT_QPA_PLATFORM=offscreen`，`ctest --test-dir build-dev -R <target> -j1`，
构建 `cmake --build build-dev -j2`（压力降级 -j1）。Oracle 独立性：期望值全部
来自手工推导/闭式解/独立参照实现（测试文件内注明），不复用被测实现。

| # | 能力 | 测试 target | 独立 oracle | exit | evidence |
|---|---|---|---|---|---|
| T1 | 类序 sorted/去重/JSON 往返 | test_class_order |

| T2 | Platt 闭式已知解（完美可分 sigmoid 收敛、B/A 有限） | test_probability_calibration |

| T3 | isotonic 单调性+PAV 已知解 | test_probability_calibration |

| T4 | Brier/ECE 手算值 + bins 总数=N | test_probability_calibration |

| T5 | entropy/margin/disagreement 手工数值（含 p=0、和≠1 输入拒绝） | test_uncertainty |

| T6 | feature schema fingerprint 稳定/顺序敏感/JSON 往返 | test_feature_schema |

| T7 | SVM OvR decision scores 符号/可分性（opt-in 开启） | test_classifier_svm（扩展） |

| T8 | NB labels 旁车往返 + 列序一致 | test_classifier_normalbayes（扩展） |

| T9 | pipeline uncertainty 栅格 known-answer + reject 掩码 | test_classification_pipeline（扩展） |

| T10 | sidecar v2 往返 + v1 读取兼容 | test_classification_pipeline（扩展） |

| T11 | 空间泄漏被 audit 捕获（random vs spatial fold 反转） | test_spatial_cross_validation |

| T12 | buffer 剔除语义（min dist > buffer） | test_spatial_cross_validation |

| T13 | group folds 组完整性 | test_spatial_cross_validation |

| T14 | 对象邻接多数平滑 known-answer 图 | test_classification_object_postprocess |

| T15 | min-area 并边规则 + NoData 永不吸收 | test_classification_object_postprocess |

| T16 | RF feature importance 非负、区分性特征更高 | test_classifier_random_forest（扩展） |

| T17 | studio 数据准备（bins/top-k pairs）known-answer | test_classification_studio_widget（扩展） |

| T18 | E2E：泄漏/不平衡/校准 Brier 下降/对象图/artifact 重放 | test_classification_intelligence_e2e |

| T19 | 100k 样本有界（RUN_SERIAL+TIMEOUT） | test_classification_intelligence_scale |

| T20 | 回归：既有分类 suites 全绿 | test_classifier_* 全家族 |


Oracle 锚点（GOAL Loop Oracle 映射）：O1→T11；O2→T1/T5/T8；O3→T10/T18(e2e replay)；
O4→T20+全家族两遍；O5→diff check；O6→Phase 8 双跑记录；O7→REVIEW_LOG。

## 最终运行结果（Phase 8 双跑，commit 395a1b96c3+，2026-09-16）

| 套件 | Pass1 | Pass2 |
|---|---|---|
| test_class_order | 0 | 0 |
| test_uncertainty | 0 | 0 |
| test_feature_schema | 0 | 0 |
| test_probability_calibration | 0 | 0 |
| test_spatial_cross_validation | 0 | 0 |
| test_classification_object_postprocess | 0 | 0 |
| test_classification_studio_widget | 0 | 0 |
| test_classifier_normalbayes | 0 | 0 |
| test_classifier_svm | 0 | 0 |
| test_classifier_mlp | 0 | 0 |
| test_classifier_random_forest | 0 | 0 |
| test_feature_scaler | 0 | 0 |
| test_cross_validation | 0 | 0 |
| test_stratified_split | 0 | 0 |
| test_accuracy_assessment | 0 | 0 |
| test_classification_pipeline | 0 | 0 |
| test_classification_intelligence_e2e | 0 | 0 |
| test_classification_intelligence_scale | 0 | 0 |

Oracle 1→T11、2→T1/T5/T8、3→T10/T18、4/6→上表两列、5→diff check clean、
7→REVIEW_LOG.md（P0=0、P1=0 after fixes）。
not-executed：T7 的 SVM OvR 断言含于 test_classifier_svm 既有套件之外的新
负/正用例因 sicnu_add_test 族被 pre-existing master 编译破坏阻塞，SVM OvR
的 known-answer 由 test_classifier_svm 直链套件内的可分性断言覆盖（直链族
已含 test_classifier_svm，exit 0）。
