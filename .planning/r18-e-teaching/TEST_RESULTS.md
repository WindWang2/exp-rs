# R18-E TEST_RESULTS

（随轮次追加真实命令/退出码/输出摘要；NOT_EXECUTED 项注明原因与补偿验证。）

| 轮 | 命令 | 退出码 | 摘要 |
|----|------|--------|------|
| RED-0 | 配置 cmake ×5 (PROJ/GSL/Catch2离线源/NETCDF 补齐) | 0 | build-r18-e 配置成功 (Ninja Debug, -j2, 全局锁) |
| RED-1 | ./build-r18-e/test_r18_e_teaching_state (未修复代码) | 0(断言) | 9 cases: 1 pass / 8 FAIL ← D6/D7 RED |
| RED-2 | ./build-r18-e/test_r18_e_verifier_rules | 0(断言) | 8 cases: 1 pass / 7 FAIL ← V2/V4/V7/V9/V5 RED |
| RED-3 | ./build-r18-e/test_r18_e_recovery | 0(断言) | 4 cases: 1 pass / 3 FAIL ← D8 RED |
| RED-4 | ./build-r18-e/test_r18_e_rubric_parity (LD_LIBRARY_PATH=见 /tmp/r18e-env.sh) | 0(断言) | 5 cases: 1 pass / 4 FAIL ← D5 RED |
| RED-5 | ./build-r18-e/test_r18_e_grader_faultlab | 0(断言) | 7 cases: 1 pass / 6 FAIL ← V1/V3/V6/V10/V8 RED |
| RED-6 | test_r18_e_completion_ledger 编译 | 失败 | 无 verdictArtifactStamp API —— 预期(修复期 API), 即 D7 修复存在的 RED 证据 |
| GREEN-1 | 10 个轻量目标 ×2 连续运行（修复后） | 0 | 全部 All tests passed（teaching_state/verifier_rules/recovery/completion_ledger/rubric_parity/grader_faultlab/studio_session/studio_projections/batch_checkpoint/golden_loop）|
| GREEN-2 | ./build-r18-e/tests/test_r18_e_help_contracts ×2 | 0 | All tests passed (62 assertions in 4 test cases) |
| 修复-中迭代 | 密度不变量改为 contiguous+unique+max==lastSeq；AtMost 镜像 (windowFrom,windowTo) 开区间；fromJson 失败清空 completed；零行 pass 拒绝清 pass 标志；help 测试注册对齐 help lane 形状 | 0 | 26 RED → 4 → 0（双跑稳定）|
| R1-fix 后双跑 | 11 个 R18-E 目标 ×2 | 0 | 全 PASS（含新增 T201e/T201f/T304f/T302b/V10b/CSV 空格用例）|
| R1-fix 回归 | test_grader_engine(320)/test_faultlab(560)/test_verifier_packs_14(303)/test_teaching_admin_core(3450)/rubric_weights/checkpoint_stamp/restore_r16_c_t05/lab_session_store | 0 | 全部 All tests passed（restart-identity 恢复后 admin_core 全绿）|
| R1-fix 迭代 | resumed 出行 JSON 破坏重启字节同一性 → 回退为进程内标志+报告级计数；CSV 测试断言改为「单元格有效首字符」检查；T302b 改双学生构造 | 0 | — |
