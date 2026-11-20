# ACCEPTANCE — R35-T04 (执行时实际验证)
## 构建配置
- cmake --preset dev-default -DOpenCV_DIR=.../opencv5 (dev-default=Debug, build-dev独立目录, 双resource_guard, -j4/-j2)
## 直跑二进制全绿 (2026-11-19 本机, QT_QPA_PLATFORM=offscreen, 每套件重复2次 exit=0)
新测试: test_r35_t04_chunk_contracts 98断言/16用例; test_r35_t04_resume_store 315/13; test_r35_t04_verifier_evidence 66/7; test_r35_t04_study_report_parse 33/4; test_r35_t04_promotion_admission 18/3
回归: test_chunk_graph 379/35(含修复fixture); test_chunk_contract_11 63/10; test_chunk_resume_11 47/8; test_chunk_adoption_11 435/14; test_execution_scale_fault_11 85断言全过exit=0; test_r15_C_chunk_admission 34/5; test_output_committer 113/14; test_mlops9_evidence 98/4; test_experiment_consistency_r4 298/11; test_study_e2e 168/1; test_r33_t04_verify_roundtrip 57/2; test_r27_t02_chunk_graph 21/9; test_runtime_chunk_boundaries_r4 50/7(含修复fixture); test_chunked_fabric_e2e 606370/16
## WP状态
- DONE(生产+测试): WP01, WP02, WP03, WP04, WP05, WP06, WP07, WP08, WP10, WP13, WP14, WP16, WP18
- ALREADY_COVERED(既有测试为证, 本次无新增量): WP11/WP12(#617回滚/注册失败回滚/PAM/sidecar: test_output_committer 14用例), WP17(EXP-1 bundle round-trip等: test_experiment_evaluation/test_r21_b_end_to_end)
- NOT_EXECUTED(会话容量, 诚实记录): WP09(scratch/lock既有#2145/#2147覆盖未加新证), WP15, WP19, WP20
## 既有测试fixture修正(2处, 意图不变)
- test_chunk_graph.cpp:334 外部取消用例 baseSpec(total=1<<30)→1<<24: 旧fixture 16*(1<<30) int溢出(UB), 新typed gate拒绝 — totalTiles仍远超100ms产能
- test_runtime_chunk_boundaries_r4.cpp:189 计数用例 TileSpec{}→最小合法几何: 旧fixture是R18-B已关闭的producer违约形状(0x0/空buffer)
## 已知环境事实
- 本机GCC16高负载下偶发ICE(dwarf2out/qmetatype), -j2增量重试即过, 与本轨代码无关
- CTest注意: sicnu_discover_tests注册无前缀case名, -R按二进制名筛选会漏跑 → 证据以直跑二进制exit code为准
