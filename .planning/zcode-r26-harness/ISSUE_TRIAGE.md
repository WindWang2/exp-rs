# ISSUE TRIAGE (2026-10-30, vs master 2610cb4741)
| Issue | 判定 | 证据 |
|---|---|---|
| #1539 P1 审批门令牌参数被丢弃 | FIXED_IN_MASTER_BUT_OPEN | e463f711a1 "typed parameter gates for decision_record; drop dead approvePendingRepair field (r20-03)" 直接落 agent_ops |
| #1540 P1 审批武装跨线程竞争 | FIXED_IN_MASTER_BUT_OPEN | src/app/agent_ops/agent_ops_control_center_dock.cpp:139,265 以 #1540 契约注释实现 coordinator 线程移交 |
| #1551 P1 plugin.load services 缺 isObject 门 | OPEN_UNFIXED(低置信) | src/plugins/framework/ 未 grep 到 services isObject 门；不在本轨包内，留后续轨（本轨聚焦 harness 闭环） |
| #1552 P1 SessionJournal 深度超限抛异常 | FIXED_IN_MASTER_BUT_OPEN | src/agent_loop/session_journal.cpp:316-341 stackLimit=128 + catch(Json::Exception) fail-closed（侦察代理逐行确认） |
| #1555 P1 worker 池持锁回调 | FIXED_IN_MASTER_BUT_OPEN | src/python/isolated/app_interface_bridge.cpp:44 (#1556)、池路径带修复标记注释 |
| #1556 P1 迁移失败泄漏 __shm_key__ | FIXED_IN_MASTER_BUT_OPEN | 同上 :44 注释 "never happened (#1556). Same cleanup..." |
| #1628/#1629/#1640 P3 技术债 | CROSS_TRACK | 债清单型 issue，R25/R23 轨持续消化，不属本轨范围 |
| #1650 P3 SDK 债 | OUT_OF_SCOPE | #1690 主项已修（见下），余项留后续 |
| #1690 P1 LD_PRELOAD '=' 绕过 | FIXED_IN_MASTER_BUT_OPEN | src/sdk/exprs/external_process.cpp:105-126 拒绝含 '='/NUL/空 key，注释直引 #1690 |
| #1691 P2 幽灵 py: 目录条目 | FIXED_IN_MASTER_BUT_OPEN | python_plugin_adapter.cpp:174 (#1691 revoke) + app_interface_bridge.h:68 |
| #1692 P2 IPC >1MiB 静默丢弃 | FIXED_IN_MASTER_BUT_OPEN | python_ipc_server.cpp:179 (#1692 注释+处理) |
| #1693 P3 agent lane 债 | OUT_OF_SCOPE | 债清单 |
| #1704 P1 MCP 沙箱驱动前缀绕过 | FIXED_IN_MASTER_BUT_OPEN | tool_catalog/workspace_containment.h:402 (#1704) + #1714 修复、tests/test_r22_agent_containment.cpp 钉；R25-C #1727 triage 同判 |
| #1705 P2 JobEngine logLines 无界 | FIXED_IN_MASTER_BUT_OPEN | R22/R25-C 报告已修（任务说明 #1705 据 R22/C 已修报告先复核）；本轨不重复 |
