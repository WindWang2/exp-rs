# TEST RESULTS — R26-harness

## RED（修复前实现，2026-10-30T16:0xZ）
- test_r26_planner_fact_gates: 2 cases failed / 4 assertions RED
  - D: verdict "feasible"==("!=feasible" 断言失败)、无 CRS blocking question
  - E: verdict "feasible"、无 band blocking question（控制组 4 断言通过）
- test_r26_capability_family_drift: 2 cases failed / 5 assertions RED
  - F1: 4 pin 失败（register_images/stack_register→io、change→classification、local_rx_anomaly→spectral）
  - F2: silentlyIo 非空（register_images, stack_register, group geometric）
- test_r26_harness_evidence_integrity: 5 cases failed / 13 assertions RED
  - A1: status=="completed"（期望 failed）、severity==warning（期望 error）
  - A2: provenance.run_id 停留 run-a2-first（期望 second）
  - A3: poll2 status=="completed"（期望 failed）、无 integrity check、损坏文件被改写
  - B: result.success==true（期望 false）
  - C: plan_id=="plan-1"（期望 plan-2）
RED 证据：以上输出直接来自修复前构建的二进制（build-r26-harness/tests/，构建于修复前源码）。

## GREEN（修复后）
- 轮1+轮2（同一二进制连跑两遍）：test_r26_harness_evidence_integrity 43 断言 / test_r26_capability_family_drift 12 / test_r26_planner_fact_gates 16 —— 全绿
- 修复中间发现并修正 2 处：A2 helper 误读嵌套层级（run_id 在 runIdentity 顶层）；D 控制组 fixture 未声明 bandRoles 被 E 门正确拦截（fixture 补角色）
- 既有回归：20 个受影响目标，初次 2 个失败（test_scientific_planner_core 4 断言 / test_scientific_planner_constraints 1 断言）——均为 band-role 门对未声明角色 fixture 的正确影响；fixture 补 bandRoles（保持原 spine/budget 意图）后全绿
- 最终验证（全部修复+还原后全量重建）：23/23 目标 PASS（3 新 + 20 回归）

## 回归变异验证（每个变异→对应测试必须转失败）
| 变异 | 内容 | 结果 |
|---|---|---|
| M1 | verification_evidence_written severity 回 warning | A1 失败×3 ✓ |
| M2 | provenance 替换禁用（stale 判定恒 false） | A2 失败×1 ✓ |
| M3 | Corrupt 分支禁用（恢复静默重算） | A3 失败×4 ✓ |
| M6 | familyMap 删 rs:register_images | F1+F2 失败×2 ✓ |
| M5 | band-role 门移除 | E 失败×2 ✓ |
全部变异后已还原（grep MUTATION src/ = 0），并全量重建再验证。
