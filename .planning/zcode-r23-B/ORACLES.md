# R23-B ORACLES — 可检验完成条件

- [ ] O0 基线新鲜：BASELINE/ISSUE_TRIAGE/OWNERSHIP 基于实时 fetch（master=ad98a7a704、PR 0、Issues 144）；push 前重新 fetch 复核并记录。
- [ ] O1 W1：master TU stub-compile RED（OV 3 错/TRT 1 错，留档）→ 修复后 GREEN ×2；ctest `test_r23_b_provider_stub_compile` 注册并实跑通过；变异反证（master TU 回喂必 FAIL）。
- [ ] O2 W2：RED 态实跑 test_r23_b_derived_output_roles（master 引擎：全部落 "failed to open input raster"）→ 修复后 6 拒绝 + 3 放行断言 GREEN ×2；test_model_runtime 全族回归不破（含 r21-B mask_threshold 用例与 #1706 羽化用例）。
- [ ] O3 W3（若实施）：注入 run 安装失败 → run_metrics 表零残留（master 上有孤儿行=RED）；正常导入等价 GREEN ×2；experiment 既有族回归。未实施则此处记 PARTIAL 及理由。
- [ ] O4 基线健康：test_io_vector_contract、test_dataset_core、test_experiment_consistency_r4、test_r22_data_security_consistency、test_r21_b_end_to_end、test_model_runtime 本轨构建下实跑（-j1），重大红逐项归因（环境/既有/本轨），不冒充通过。
- [ ] O5 双独立评审：R1/R2 分离上下文 subagent，逐 hunk 审 origin/master...HEAD diff + 实跑关键族；P0-P2 闭环，修复提交再审。
- [ ] O6 冲突：git diff --check 干净；无 <<<<<<< 残留；tests/CMakeLists.txt 兄弟注册块完整；不丢 R20-R22 已修代码（抽验 W1/W2 涉及文件中 #1706/#1709/#1673 标记仍在）。
- [ ] O7 权属：diff 100% 在 OWNERSHIP 白名单；共享文件仅 append。
- [ ] O8 交付：commit+push+非 draft PR（base=master，body 含 BASE/HEAD/范围/测试/评审/风险/issue 状态/复现命令）；不 auto-merge、不等 CI。
