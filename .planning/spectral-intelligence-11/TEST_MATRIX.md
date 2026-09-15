# TEST_MATRIX — spectral-intelligence-11

每项能力 → 独立 oracle → 命令 → exit → evidence。（Phase 2 起填充 exit。）

| 能力 | 独立 oracle | Test target | 命令 | exit #1 | exit #2 |
|---|---|---|---|---|---|
| A 双窗 RX 解析真值 | 高斯背景闭式 Mahalanobis；注入异常 exact score | test_spectral_local_rx | ctest -R test_spectral_local_rx -j1 | | |
| A 窗口边界/NoData | 手工小窗枚举、有效样本下限 | test_spectral_local_rx | 同上 | | |
| B sparse 解混 ℓ1 真值 | 小型系统枚举解 KKT/软阈值闭式；纯像素 exact | test_spectral_sparse_unmixing | ctest -R test_spectral_sparse_unmixing | | |
| B 病态端元 fail-closed | 共线/零范数/条件数 | test_spectral_sparse_unmixing | 同上 | | |
| C hybrid 已知角 | 构造谱：SID=0、SAM=0、正交、比例 | test_spectral_hybrid_similarity | ctest -R test_spectral_hybrid_similarity | | |
| C 波长缺失拒绝 | 无 FWHM/无 grid → typed refusal | test_spectral_hybrid_similarity | 同上 | | |
| D 端元聚类/去冗余 | 手工簇 + 阈值枚举 | test_endmember_analysis | ctest -R test_endmember_analysis | | |
| D 角矩阵对称性/对角 0 | 独立 spectralAngle 重算 | test_endmember_analysis | 同上 | | |
| D 传感器投影守 provenance | license/digest 继承断言 | test_endmember_analysis | 同上 | | |
| E artifact 链路 | placeholder 语法 + digest 不变 | test_spectral_pipeline（既有）+ 新链路 test | ctest -R spectral_pipeline | | |
| G capability drift | descriptor ↔ 算子注册一致 | test_capability_drift（既有） | ctest -R capability_drift | | |
| H 1024 band 规模 | 独立真值 + determinism + 内存上界 | test_spectral_scale | ctest -R test_spectral_scale | | |
| F workbench offscreen | widget 加载 artifact、选择联动 | test_spectral_workbench_panel | ctest -R spectral_workbench -j1 | | |
