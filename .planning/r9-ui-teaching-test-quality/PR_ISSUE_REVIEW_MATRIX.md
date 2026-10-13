# PR/Issue/Review 对照矩阵（随进度更新）

| 来源 | 项 | 状态 |
|------|-----|------|
| #1474 T0 (fault_transforms.cpp:392) | C1-1 | 进行中 |
| #1474 T1 (guided_workflow_widget.cpp:493) | C1-2 | 进行中 |
| #1474 T2 (rs_scan_pool.cpp:57) | C1-3 | 进行中 |
| #1474 T3 (script_adapters.cpp:56) | C1-4 | 进行中 |
| #1474 T4 (script_adapters.cpp:131) | C1-5 | 进行中 |
| #1453 | rs_job_runner 已删(#1474 3112cfb94), owner cleanup 已修; 本波仅 tombstone 语义 | 去重 |
| #1464 | grader timestamp 已修; faultlab optional param real-effect 已有测试; 本波补 fail-before-mutate | 部分去重 |
| #1466 | stdout cap/discovery 已修(#1474); alias+truncation 即 C1-4/C1-5 | 部分去重 |
| #1465/#1447 | master 已修 broker cache/dedup suffix/json typed | ALREADY_FIXED 待核验 |
| #1468 | sicnu_test_main 已 return result; 其余待 C2 审计 | 待审计 |

## C2 去重审计结论（subagent-① 于 2026-10-12，master=8a086427）
ALREADY_FIXED_IN_MASTER（不重做）:
- #1453 rs_job_runner 删除 (3112cfb94) + RsScanPool destroyed-hook (rs_scan_pool.cpp:33-58)
- #1464 grader 时间戳 (grader_matcher.cpp:53-129, test_grader_engine.cpp:301-376) + faultlab cell_size real-effect
- #1465/#1447 dedup suffix (terminology_provider.cpp:181-204) + broker typed-failure cache (broker.cpp:261-278) + Json typed 站点 + 帮助资源校验 (test_help_integrity_12.cpp)
- #1466 lab_session_state 原子写 (lab_session_state.cpp:201-212→durable_sidecar) + stdout cap (#1474)
- #1468 全部五子项 (return result/SO_RCVTIMEO/netcdf guard/http_range_server 计费/process_scratch atexit)

PARTIAL / NOT_FIXED（后续 wave 候选，本波不做）:
- #1466 batch_assessment discovery contains-规则静默丢弃学生文件、报告无 skip 记账 (batch_assessment.cpp:270-297) — NOT_FIXED
- #1465 refreshRecipes 链路: recipe_router.cpp:112-150 标量 get().asString() 无类型门 + agent_adapter.cpp:38-48 static-init 无兜底 — PARTIAL
