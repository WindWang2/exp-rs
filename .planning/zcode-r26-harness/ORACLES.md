# ORACLES — R26-harness（RED→GREEN×2 计划）
| Oracle | 测试 | RED 前状态 | 预期 |
|---|---|---|---|
| A1 证据写失败必须 fail-closed | test_r26_harness_evidence_integrity "A1" | status=completed（虚假成功） | status=failed + error 级 check |
| A2 provenance 随最新 run | 同文件 "A2" | 换 run 后 provenance.run_id 仍 runA（陈旧证据） | run_id==runB；engine sidecar 字节不变 |
| A3 损坏证据不得静默重算 | 同文件 "A3" | poll2 静默重算→completed 且改写损坏文件 | poll2 failed + integrity check + 文件字节保持 |
| B 删除失败 typed failure（生产注册面） | 同文件 "B" | success=true 且 ok:false 藏 payload | success=false + errorCode |
| C 重绑刷新 plan 身份 | 同文件 "C" | plan_id 停留 plan-1 | plan-2；空身份保持旧值 |
| D 未知 CRS 不可判对齐 | test_r26_planner_fact_gates "D" | crsSet 忽略空→verdict feasible | blocking question(CRS) + verdict!=feasible |
| E band_roles 契约门 | 同文件 "E" | bandRoles 事实弃用→feasible | blocking question(band) + 降级；声明角色控制组保持 feasible |
| F family 漂移 | test_r26_capability_family_drift F1/F2 | register/stack_register→io、change→classification、local_rx→spectral | 10 id 正确 family；无 unknown-group 静默 io |
| G 错误表重复键 | 编译期（std::map 初始化列表重复键不诊断→改为静态断言式去重测试） | harness_error.cpp:67,82 重复 | 无重复 |
回归变异验证：GREEN 后逐项对 A1(severity 回 warning)、D(移除 blocking)、F(删一个 familyMap 条目) 做临时变异，确认对应测试转 RED。
