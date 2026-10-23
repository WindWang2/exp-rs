# R20-03 TASKS — 证据驱动工单

基线 `28e1891390`。状态机：`RED 已复现` → `FIXED (GREEN×2)` → `REVIEW-ONLY` / `DEBT` / `ALREADY-FIXED`。
每项：path → 缺陷机理 → reproducer → oracle。

## W1 ★ [P2][C02/C09] IPC Response `result` 类型门（UB 防线）
- path: `src/sdk/exprs/ipc_envelope.cpp:218-221`（对照 Request params 门 :200-204）
- 机理: worker→host 帧是不可信输入（宿主自身契约），非对象 `result` 经 `plugin_host_proxies.cpp:201`、`plugin_host_process_runtime.cpp:207/551/650` 的**非 const operator[]**（jsoncpp resolveReference）→ Debug 下 JSON_ASSERT abort / NDEBUG 下 union 野指针读——两类都不可 catch。合法 worker 恒发对象，但恶意/坏版本 worker 可发任意帧。
- reproducer: `tests/test_exprs_ipc.cpp` 新 TEST_CASE「successful response envelope refuses a non-object result (E6002)」——master 上 string/array/int/bool result 全部 decode 成功（RED）；修复后 typed E6002 拒绝，null/absent/对象保持合法。
- fix: decode 时 `!isObject() && !isNull()` → "response result must be an object (E6002)"。

## W2 ★ [P2][C11/C12] capability sidecar 加载器 fail-closed 契约击穿（3 文件同类）
- path: `src/agent/harness/capability_catalog.cpp:654,926`、`capability_relations.cpp:103`、`capability_knowledge.cpp:191,199,520-522`
- 机理: 加载器设计=「逐文件记问题、跳过坏文件」；但 schema_version/id/family/kind 的 asInt()/asString() 在问题门之前/之外抛 Json::LogicError → 单文件单字段类型错中止整个 reload()；MCP 路径吞成 generic error，**GUI catalog panel/copilot 直连单例的路径无 catch → 进程终止**。
- reproducer: `tests/test_capability_knowledge.cpp` ①validator 级扩展（string/array schema_version、object id → 必须记名问题）②新 loader 级 TEST_CASE（临时目录 good+bad 文件并存 → reload 不抛、只丢坏文件、记名问题）。
- fix: 全部改 isIntegral()/isString() 门；knowledge validateEntry 加 extends 类型门（防护查询期 extends 链行走）。

## W3 [P3][C03] decision_record 工具参数类型门
- path: `src/agent/harness/grounding_tools.cpp:473-509`
- 机理: action/subject/kind/status/note/decision_id/chosen 裸 asString() → 类型错抛 Json::LogicError → MCP 边界 generic catch，错误契约降级（同文件兄弟工具 UnderstandTool/ContextTool 都有 isString 门）。
- reproducer: `tests/test_harness_evals.cpp` 新 TEST_CASE——master 上 array action / object subject / int decision_id / array kind 抛异常（RED）；修复后 typed INVALID_PARAMETER 且 error 文本含字段名。
- fix: optionalString 门卫 lambda，类型错 → 命名字段的 typed failure。

## W4 [P3][C02] TooLarge 诊断 hexdump 恒空
- path: `src/sdk/exprs/ipc_frame.cpp:147-154` + `ipc_channel.cpp:714-727`
- 机理: IpcFrame::read 先 `payload.clear()` 再在 cap 检查处返回 TooLarge——payload 永远空；channel 的 "; stream head: " 后永远无字节，协议诊断撒谎。
- reproducer: `tests/test_exprs_ipc.cpp` frame-cap 用例扩展——master 上 salvaged.size()==0（RED）；修复后 payload 携带 4 字节长度前缀（AB CD EF 7F 断言）。
- fix: TooLarge 返回前 `payload.assign(pending, 0, kPrefixBytes)`（前缀是唯一真实的流头证据——帧体在 cap 检查处从未被读）。

## W5 [P3] OpsRunRequest 死字段清理
- path: `src/agent_ops/operations_coordinator.h:40`
- 机理: #1539 修复（driver 拒裸布尔、走令牌路径）后 `bool approvePendingRepair` 全库零读零写（grep 实证）。与 R19-02 T2（guidance_ 死成员 #1636）同类。
- oracle: 删除后编译 + test_agent_ops_core ×2 全绿（结构性 RED=字段存在且无引用）。

## W8 [验证债][C09] plugin_host 进程族测试全量
- R19-02 NOT_EXECUTED 项。构建完成后跑 test_plugin_host / test_plugin_host_allowlist / test_plugin_host_boundary_r4 / test_plugin_host_process / test_plugins_runtime_host / test_plugin_channel_parity_r4 / test_plugin_execution_barrier / test_plugin_lifecycle_* / test_python_plugin_host（RUN_SERIAL）等，×1 记录（进程族允许单轮，因时长）。

## CT-6 [C14] surface snapshot registry 腿证据新鲜化
- 跑 tests/test_surface_snapshot：projection+wire 腿应绿；registry 腿预期 KNOWN_BASELINE_FAIL（7 cartography 工具，04 轨红区），失败信息必须含工具名。

## DEBT（记录不修，附原因）
- D5 mProcessHandle 选举败者无锁读（plugin_host_session.cpp:630,701）：与 #1644 mLastKnownGroup 同类；无可确定性 RED（数据竞争 UB 仅 TSAN 可见），按任务书「不能造红时记录原因」处理。
- D6 种植文件信任域无界读（workflow_run_lock/capability_mirror/plugin manifest 系）：impact=内存耗尽；修复面 6+ 文件，本轮容量不足，记录待后续轮（bounded-reader 先例已存在于 checkpoints 路径）。
- D8 ClearGuard 整除 m_activeTaskIds（R19-02 R12）：ExecutionHandle::cancel 与 requestCancel 汇聚同一 TaskCenter::cancelTask（execution_plane.cpp:448-456），行为无差异 → 无法造红。
- D9 CLI 审批面（src/cli 灰区）：`--approve` 布尔无人消费（静默 no-op）、`--approve-pending-repair` 必触发 APPROVAL_TOKEN_REQUIRED、无 --repair-approval 令牌通道 → CLI 侧人工审批门不可达。跨区报告（CROSS_TRACK_FINDINGS.md），本轨不修。
- #1628-3 temporal phenology 接线、#1629-1 run/resume 其余重复、#1630-2 EnumTable：维持 R19-02 DEBT 决策。
