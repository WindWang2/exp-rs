# TASKS — R26-harness（每包 RED→最小修复→GREEN×2→评审）
| # | 包 | 文件 | Oracle | 状态 |
|---|---|---|---|---|
| A1 | verification 证据写失败→error 级→status=failed | plan_tools.cpp:1519 | sidecar 路径不可写时 runResultDocument status=="failed" | TODO |
| A2 | 同路径重跑换新 run：harness provenance 随之原子替换 | evidence.cpp:125 | 第二次 run 后 provenance run_id==runB；engine DerivationRecord 侧仍不被覆盖 | TODO |
| A3 | 本 run 证据 sidecar 损坏→typed integrity FAIL，不静默重算/改写 | evidence.cpp:206 readVerificationEvidence | poll2 status=="failed" 且含 integrity 原因；损坏文件不被覆盖 | TODO |
| B | workflow_session delete 失败→typed failure（生产注册面 E2E） | context_checkpoint.cpp:581 | 经 SpatialToolRegistry find("harness:workflow_session")->execute 删除不存在会话 → failure | TODO |
| C | ledger 重绑刷新非空 plan_id/goal/intent | context_ledger.cpp:41 | rebind plan-2 后 binding.plan_id=="plan-2" | TODO |
| D | planner：多资产含未知 CRS→blocking question+降级 | planner_core.cpp:318 | crs ""+EPSG:4326 两 consumable → open question + verdict!=feasible | TODO |
| E | planner：band_roles 契约要求+资产未声明角色→blocking question+降级 | planner_core.cpp analysis 选择 | wavelengthPolicy=band_roles 且无 band_roles 事实 → question + verdict!=feasible | TODO |
| F | capability familyMap 补 10 算子+漂移守卫测试 | capability_catalog.cpp:170 | canonicalFamily 10 id 正确；任意 catalog 条目 group 未知且无 familyMap 项 → 测试失败 | TODO |
| G | harness_error WAVELENGTH_INCOMPATIBLE 重复项去重 | harness_error.cpp:82 | 表内无重复键（编译期 map 结构自证） | TODO |
