# R20-03 ISSUE_TRIAGE — open issue 逐条现码分诊（2026-10-22/23 live）

格式：`(issue, 实际路径/行, 状态, 证据, 处置)`。`open ≠ 未修`——每条以 live master 源码为准。

## A. 本轨域 P1/P2（任务书点名种子）
| issue | 位置 | 状态 | 证据 | 处置 |
|---|---|---|---|---|
| #1539 审批门空转（裸布尔无人读） | ops_driver.cpp:40-66 readApprovalArgs | **ALREADY-FIXED** | live 复核：拒绝 `approve_pending_repair`（APPROVAL_TOKEN_REQUIRED）、读 repair_approval+approval_now_ms 令牌路径；coordinator 启动时消费 driver-held token（operations_coordinator.cpp:476-505） | 无需重修；残留死字段见 W5 |
| #1540 审批武装跨线程竞争 | operations_coordinator.h:200-229 线程契约 + dock startSession | **ALREADY-FIXED** | live 复核：dock 改为把 token 按值交 worker，在 coordinator 线程 arm（agent_ops_control_center_dock.cpp startSession 注释明示 #1540） | 无需重修 |
| #1552 journal 深嵌套异常逃逸 | session_journal.cpp:320-341 | **ALREADY-FIXED** | live 通读：stackLimit=128 + catch(Json::Exception) + fail-closed | 保护性回归在既有测试 |
| #1589 science_context 失效传播未接线 | data_platform_tools.cpp notifyProjectSwitch | **ALREADY-FIXED** | R19-02 复核 + #1666 恢复后 broker shared_ptr 快照集成正确（本轮 sweep 复核 agent_adapter.cpp:211-214 持快照全程） | 无需重修 |
| #1594 placeholder 复杂度 | placeholder_grammar.cpp 单遍 | **ALREADY-FIXED** | R19-02 复核 :232-252 | — |
| #1592 plugin params 类型 | plugins worker params 前置类型检查 | **ALREADY-FIXED** | R19-02 复核（本轮 sweep 复核 peerString 门在 worker 侧） | — |
| #1654 workflow cancel 一次性毒化/残余竞态 | workflow_runtime.cpp:226-245 消费 + :810-830 store 同锁 | **ALREADY-FIXED** | live 复核注释明示 R19-02 全序修复 | 既有回归网守护 |

## B. 本轨域 P3 技术债（swarm 评审批次 #1628-#1654）
| issue | 项 | 状态 | 证据 | 处置 |
|---|---|---|---|---|
| #1628-1 StdinReader EOF/超长 | mcp_server.cpp:422-468 | ALREADY-FIXED | R19-02 复核 gcount 区分 | — |
| #1628-2 自由文本路径豁免 | mcp_server.cpp:164-203 | ALREADY-FIXED | R19-02 复核 | — |
| #1628-3 temporal phenology 工具未接线 | temporal_spatial_tools.* + temporal_tool.h | **仍 OPEN（设计如此）** | live 复核：registry kBuiltinTools 无 phenology 三工具；D-160-1 声明为后续集成 | 维持 R19-02 DEBT 决策（接线=新功能面，非修复）；无锁 static store 在接线前无害 |
| #1628-4 workspace_snapshot 注释虚 producer | workspace_snapshot.cpp:89-94 | ALREADY-FIXED | live 复核：注释已诚实声明 "No producer fills them anywhere in the tree" | — |
| #1628-5 data_platform_tools using 收敛 | — | ALREADY-FIXED | R19-02 | — |
| #1629-1 run/resume 启动重复 | operations_coordinator.cpp | **部分化解** | live 复核：checkpoint sink 已提取 installCheckpointSink 共享助手；其余重复属重构债 | DEBT（Shotgun Surgery 重构风险>收益，维持 R19-02） |
| #1629-2 actions 漏登 | session_surface.cpp:52 | ALREADY-FIXED | R19-02 | — |
| #1629-3 checkpoint sink 注释矛盾 | operations_coordinator.cpp:115-146 | ALREADY-FIXED | live 复核：注释改写为同步调用安全论证 | — |
| #1630-1 agentbench replay usage | — | ALREADY-FIXED | R19-02 | — |
| #1630-2 EnumTable 四份拷贝 | agentbench | DEBT | 纯重构 | 不修（R19-02 决策维持） |
| #1634 contracts 全 5 项 | — | ALREADY-FIXED | R19-02 | — |
| #1643/1644/1645/1649/1650 | planner/plugins/preflight/science_context/sdk | ALREADY-FIXED | R19-02 | — |
| #1570-1574 agent/agent_ops P2 批 | — | ALREADY-FIXED | R19-02（本轮对 #1573/#1574 抽查一致） | — |

## C. 跨轨（只读证据，不修）
- CT-6（R19-02 遗留）：7 个 cartography 工具 raw schema 缺项 → 04 轨红区；本轨保持 KNOWN_BASELINE_FAIL 证据新鲜（见 TEST_RESULTS）。

## D. 新发现缺陷（本轮两路并行只读深扫，全部本轨写区）
| ID | 严重级 | 位置 | 缺陷 | 工单 |
|---|---|---|---|---|
| D1 | P2 | src/sdk/exprs/ipc_envelope.cpp:220 | IPC Response `result` 不做 isObject 门（Request 侧对 `params` 有门 :200-204）→ 非对象 result 经 plugin_host_proxies.cpp:201 / plugin_host_process_runtime.cpp:207/551/650 的**非 const operator[]** 触发 JSON_ASSERT/UB（worker 帧不可信域） | W1 |
| D2 | P2 | src/agent/harness/capability_catalog.cpp:926,:654；capability_relations.cpp:103；capability_knowledge.cpp:191,199 | sidecar 加载器 asInt/asString 在 problems 收集门**之前**抛 Json::LogicError：单文件单字段类型错 → 整个 reload() 中止，"记录问题跳过坏文件"的 fail-closed 契约被击穿；非 MCP 调用方（GUI catalog panel/copilot）无 catch → 崩溃 | W2 |
| D3 | P3 | src/agent/harness/grounding_tools.cpp:475-498 | decision_record 工具对 action/subject/kind/status/note/decision_id/chosen 裸 asString()（同文件兄弟工具都有 isString 门）→ 类型错变 generic error 而非 INVALID_PARAMETER | W3 |
| D4 | P3 | src/sdk/exprs/ipc_channel.cpp:714-727 + ipc_frame.cpp:116,147-154 | TooLarge 诊断 hexdump 读已 clear 且未填充的 payload → "stream head:" 永远为空，诊断撒谎 | W4 |
| D5 | P3 | src/plugins/host/plugin_host_session.cpp:630,701 vs 724,787,1179 | kill 选举败者无锁读普通 mProcessHandle 指针（同类缺陷 mProcessGroupId 已用 mLastKnownGroup 修过 #1644） | W6（防御性，与 #1644 同款模式） |
| D6 | P3 | workflow_run_lock.cpp:51,167; preflight/capability_mirror.cpp:293; sdk plugin_manifest/registry/discovery/snapshot 读 | 种植文件信任域的无界文件读（checkpoints 已有界，旁路无） | W7（选最高频路径加界） |
| D7 | P3 | operations_coordinator.h:40 | `approvePendingRepair` 死字段：#1539 修复后全库零读零写 | W5 |
| D8 | P3 | workflow_runtime.cpp:434-446（R19-02 RISK R12） | 同 session 并发 runStep 的 ClearGuard 按 key 整除 m_activeTaskIds | **不修**：ExecutionHandle::cancel 与 requestCancel 直接路径汇聚同一 TaskCenter::cancelTask（execution_plane.cpp:448-456 live 复核），取消仍由 flag watcher 20ms 内兑现，**无行为可观察差异 → 无法造红**；记录于 LOOP_LEDGER |

## E. 验证债（R19-02 NOT_EXECUTED）
- plugin_host 进程族测试全量（test_plugin_host / host_allowlist / host_boundary_r4 / host_process / plugins_runtime_host / channel_parity_r4 …）→ 本轮补跑（W8）。
