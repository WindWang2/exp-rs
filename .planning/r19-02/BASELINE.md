# R19-02 BASELINE — 模块调用图、依赖与既有不变量（基线 e12a9afbc）

## 1. 本轨核心链路（Pi → MCP → Catalog/事实 → Workflow/TaskCenter → SDK/Plugin → 反馈）
- `pi/exp-rs-spatial.ts`（Pi 扩展壳，唯一 import `pi/mcp_bridge.ts`，no_drift 强制单实现）→ 以 `--mcp` spawn 宿主二进制，MCP stdio JSON-RPC。
- `pi/mcp_bridge.ts`（McpBridge 类）：代际(generation)化子进程管理——failed-start teardown、stdio 4MiB 行界、EOF 半帧、runaway >32MiB kill+lazy respawn、checkpoint poison、restart-generation 帧缓冲重置；`nextId` 单调不重置（跨代唯一）。
- `src/agent/mcp_server.cpp`：StdinReader（干净 EOF gcount 区分超长 #1628-1）→ onLineRead（-32700/-32600）→ handleRequest（null-id 请求 #644、method 必填 -32600、未初始化门 -32002、CRLF/ANSI 清洗 #1186、notifications/cancelled → rpcCancelMapKey 类型化键 R18-C C05-b）→ Tool Catalog / workflow / agent_ops 分发。validateWorkspacePaths 自由文本槽豁免（#1628-2）。
- `src/agent/tool_catalog/**`：surface_registry（tools/list 与 get_tool_schema 同一投影权威，R18-C C01）、surface_progress（RateLimiter 0.05 阈值、终态单发、状态变更必发）。
- `src/science_context/**`：ScienceContextBroker（capabilityFacts/recipes/cache；**#1505-P1 修复曾被 5e14bd1d7b 回滚，本轨 T0 恢复**）；asset_state_provider（allowAssumed 出站投影，#1549 已修）；recipe_router（空 needle 守卫，#1649 已修）；data_platform_tools 接线 notifyProjectSwitch（#1589 已修）。
- `src/preflight/**`：runtime_adapter（temporal 多集合截断计数 #1588 已修）、rules.cpp（isoDayNumber daysInMonth 门 #1645 已修）。
- `src/workflow/**`：workflow_runtime（runStep 两路径：ExecutionPlane+cancelWatcher(20ms poll) / sync context poll；consume-at-start #1654 已修，**残余无序窗口本轨 T1 关闭**）；workflow_run_coordinator（RAII TerminalHandoutGuard、输出身份检查、cancelRun→Canceled+checkpoint）；workflow_checkpoint（原子写、poison 分类拒载）；placeholder_grammar（单遍不重扫 #1594 已修）。
- `src/plugins/**` + `src/sdk/**`：plugin_host_worker_main（E1002 类型化拒绝 #1551/#1592 已修）、plugin_host_session（killProcess liveness-exchange + mLastKnownGroup #1644 已修）、external_process（pipe fd 全回收 #1650-1 已修）、ipc_channel（cancelPredicate catch+erase #1650-2 已修）。
- `src/agent_loop/**`：session_journal（stackLimit=128+catch #1552 已修）。
- `src/agentbench/**`：trace/case_schema/suite（usage 先计后 continue #1630-1 已修）。
- `src/contracts/**`：text_scan.insideLiteralOrComment 被 census/error_code_scanner 全匹配点使用（#1634 已修）；fromCanonicalJson outputs 走 paramFromJson（#1606 已修）。
- `src/explain/**`：explanation_validator（**guidance_ 死成员 #1636 本轨 T2 清理**）；numberToString 有限性门（#1616 已修）。

## 2. 关键线程/锁/生命周期不变量（已合并且必须不回退）
1. broker capabilityFacts 返回 shared_ptr 快照；重装 swap 指针不改已交出对象（T0 恢复后）。
2. mcp_server: -32700 行界/EOF 区分；cancel map typed key；progress 真尺度 0..1 + 0.05 限流。
3. workflow_runtime: runStep 入口消费陈旧取消 + m_mutex 全序（T1 后）；PlaneTaskFailure 不静默重跑（#1459）；ClearGuard 清 m_activeTaskIds。
4. coordinator: 终态单发、cancel→durable checkpoint、resume 拒绝非终态。
5. plugin host: 请求 deadline ladder、worker write-gate fail-closed、kill 幂等（POSIX ESRCH）。
6. 生成物管线：pi/knowledge pages = sidecar 派生（ADR 0154），committed==derived 漂移门 + 32KiB/页预算门。

## 3. 构建环境（TEST_RESULTS.md 详录）
- cmake 3.30.5 + Ninja，Debug，ENABLE_TESTS=ON；GDAL/PROJ/GEOS/GSL/NETCDF/QCA 来自外部 vendor prefix（r18-c 同款）；sqlite3/zstd/pcre2-8 钉系统 .so（vendor 的是非 PIC .a）。
- 全局锁 /home/kevin/project/exp-rs/.git/r19-global-j2-build（三 r19 轨共享），-j2、ctest -j1、QT_QPA_PLATFORM=offscreen。
- CI 自 2026-10-11 全仓失效（#1491）——一切本地验证。

## 4. 既有测试资产（本轨回归网）
tests/: test_mcp_server, test_surface_{parity,snapshot,protocol,protocol_r4,e2e}, test_workflow_{cancel,execution_plane,recovery}, test_r14_C_mcp_workflow_cancel, test_agent_ops_{core,task_center_executor,benchmark_writer}, test_agent_loop_seams, test_plugin_{host,host_allowlist,capabilities}, test_science_context_* (broker/concurrency/live_authorities/...), test_preflight_*, test_contract_projection_9, test_explain_*, test_agentbench_*, test_capability_knowledge（漂移门）。
pi/test/: mcp_bridge, bridge_parity, no_drift, r18_c_* (id_null/timeout_cancels), scientific_workflow_compiler_11（含 32KiB 预算门）, surface_snapshot。
