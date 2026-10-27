# ISSUE_TRIAGE — r23-C（144 open issues 逐域核验；仅列与本轨相关域）

方法：`gh issue list --state open`（144 条）→ 按本轨白名单域筛出候选 → 两轮独立只读 subagent + 主会话直接读码核验（对照当前 master 代码而非 issue 文本或提交信息）。判定词汇：LIVE / FIXED_OPEN / PARTIAL / STALE / OUT_OF_SCOPE。

## A. 高优先 P1/P2（第一轮 subagent 核验，全部 FIXED_OPEN）

| Issue | 标题摘要 | 判定 | 证据（当前代码） |
|---|---|---|---|
| #1539 | 审批门空转：令牌参数被丢弃 | FIXED_OPEN | ops_driver.cpp:43-63 readApprovalArgs 读 token；APPROVAL_TOKEN_REQUIRED 拒裸布尔；CLI --repair-approval 已接；test_cli_agent_ops.cpp:214 |
| #1540 | 审批 arming/lastResult 跨线程竞争 | FIXED_OPEN | operations_coordinator.h:199-213 单线程契约；dock 只记 token，worker 线程内 arm（#1540 注释在树） |
| #1570 | spectral 工具 schema 误声明 layer_id required | FIXED_OPEN | spectral_spatial_tools.cpp:136-175/306-341 无 required 键；test_spectral_agent_tools.cpp:283 断言 |
| #1571 | undo/redo 持锁 emit | FIXED_OPEN | workspace_commands.cpp:85-132 锁内弹栈、锁外回调（注释 #1571） |
| #1573 | onReplyError 缺 streamGeneration 护栏 | FIXED_OPEN | llm_streaming_client.cpp:488-513（注释 #1573，QPointer+generation 门） |
| #1574 | apply() 只捕 Json::LogicError | FIXED_OPEN | 抛异常调用已改 error_code 重载（ops_driver.cpp:79-86/257-268，注释 #1574） |
| #1552 | SessionJournal::load 深度炸弹逃逸 | FIXED_OPEN | session_journal.cpp:313-341 stackLimit=128 + catch Json::Exception fail-closed；test_agent_loop_core.cpp:425 |
| #1558 | 回归用例断言恒真 | FIXED_OPEN | test_e2e_open_issues.cpp 重写为真实断言（0847c5ec1e） |

## B. R17-agent 域（#1704/#1690/#1691/#1692/#1705 — r22-agent PR #1714 已修，核验 FIXED_OPEN）

主会话直接读码：appendLog 有界化+droppedLogLines 账目（job_engine.cpp:752-790）、delta 光标绝对空间（:960-1000）、terminal delta 转换（:1108-1122）、TerminalOutcome 收敛（#1640, :1035-1080）、workspace_containment.h R2 批注与 tiledb/path=/driver= 修正（:195-230/295-320）、StdinReader 干净 EOF 区分（#1628-1, mcp_server.cpp:425-468）。全部 FIXED_OPEN。

## C. P3 技术债清单（第二轮 subagent 逐项核验）

| Issue 项 | 判定 | 备注 |
|---|---|---|
| #1628-1 StdinReader EOF 假 -32700 | FIXED_OPEN | gcount==0+eof break 在树 |
| #1628-2 prose 字段误拒 PATH_OUTSIDE_WORKSPACE | FIXED_OPEN | isFreeTextSlot 白名单在树（mcp_server.cpp:171-228） |
| #1628-3 temporal 工具未接线 | OUT_OF_SCOPE | DECISIONS D-160-1 声明的后续集成，非缺陷修复轨 |
| #1628-4 workspace_snapshot 幻影 producer 注释 | FIXED_OPEN | 注释已改写为如实描述 |
| #1628-5 三重 using 堆叠 | FIXED_OPEN | data_platform_tools.cpp:77-84 已收敛 |
| #1632-1 terrain 死 QFutureWatcher | FIXED_OPEN | 成员已删 |
| #1632-2 输出路径双份派生分叉 | FIXED_OPEN | defaultOutputPath() 共享在树 |
| #1632-3 双 SpectralProfileWidget | PARTIAL | 缺陷部分（include/线程池）已修；exp_gui 重复类仍存但仅测试使用、零生产用户——删除属清理决策，非缺陷，本轨不动（记 RISK_REGISTER） |
| #1632-4 gui_job_adapter 三重复 | FIXED_OPEN | submitWithCallbacks 共享在树 |
| #1632-5 clampText left() | FIXED_OPEN | 已用 truncateSummary |
| #1632-6 erase 工具死状态上限 | FIXED_OPEN | mStampCount 上限在树 |
| #1639-1/2 help 截断/上界 | FIXED_OPEN | truncateSummary 已复用；definitionZh 现不越界 |
| #1640 终态四重复 | FIXED_OPEN | TerminalOutcome 在树 |
| #1646-1 GDAL 绕过 ensureGdalInit | FIXED_OPEN | ensureGdalInit 已接（#692 注释） |
| #1646-2 local_worker_pool 无锁读 m_running | **PARTIAL→LIVE(形式UB)** | 注释承认竞态但未消除（见 WP-4 候选） |
| #1646-4 重复前向声明 | FIXED_OPEN | 已收敛 |
| #1646-3/5/6 | OUT_OF_SCOPE | src/processing/gdal|根|algorithms 非本轨白名单 |
| #1650-1 pipe() 部分失败 fd 泄漏 | FIXED_OPEN | 循环 close 在树（external_process.cpp:1196-1208） |
| #1650-2 cancelPredicate 异常泄漏 Pending | FIXED_OPEN | try/catch+erase 在树（ipc_channel.cpp:280-295） |
| #1652-1 rubric 双 schema 分叉 | FIXED_OPEN | validateProcessRubric 已委托引擎 GradingRubric::fromJson |
| #1652-2 student_ 前缀自误报 | FIXED_OPEN | owner token 白名单 + Unicode 边界在树 |
| #1653 beginResult 四份私有拷贝 | FIXED_OPEN | 三文件已 include checks_numeric.h 复用 |
| #1654 取消标志毒化会话 | FIXED_OPEN | runStep 消费时 store(false) 在树 |
| #1655 CP11 渲染门无归属 | STALE(评估) | 5 渲染用例仍永久 SKIP；启用需健康主机实跑验证 legend paint 挂死是否仍在——风险高于收益，本轨不触碰渲染门（记 RISK_REGISTER） |
| #1693 agent 域债 2 项 | FIXED_OPEN | 死分支已删（含注释）；terminal 不可变护栏在树 |
| #1695-17a async runner 死代码 | FIXED_OPEN(缺陷) | QPointer/endRun/dtor 已修；类保留（死代码清理另议） |
| #1695-17b entr%2 复数 hack | **LIVE** | teaching_admin_dock.cpp:439-444 → WP-3 |
| #1695-17c GUI 线程同步发现+dtor join | LIVE(M) | 超本轨最小修复预算，记 RISK_REGISTER 转交 |
| #1695-17d recordAnswer 空白答案 | OUT_OF_SCOPE | src/lab/session_state.cpp 非本轨白名单 |
| #1695-17e 批量输出 JSON/CSV 非原子 | **LIVE** | batch_assessment.cpp:868-955 → WP-2 |

## D. C05 指定复查项：R22-agent REVIEW_R2 遗留发现

| 发现 | 判定 | 处置 |
|---|---|---|
| R2-F3 [P3] TaskCenter cumulative/delta 索引空间混用（丢行+重行） | **LIVE**（主会话读码证实：累积分支仍 `size() > forwarded` + `forwarded := size()`） | **WP-1 修复** |
| R2-F4 文件驱动表不可达项/tiledb 短名 | FIXED_OPEN | workspace_containment.h R2 批注在树 |
| R2-F5 未知驱动 kvKeys 缺 path=/driver= | FIXED_OPEN | kvKeys 已含 |
| R2-F6 [P3] 引擎内部 append 后 cumulative 守卫失真（终态行不可达） | **LIVE**（同根：retained size vs 绝对 forwarded） | **WP-1 同一修复收口** |
| R2-F7 StdinReader 平台注意项 | FIXED_OPEN（libstdc++ 已核） | — |
| R2-F8 测试注册小疵 | FIXED_OPEN | — |

## E. 其余 open issues（非本轨域）
#1697/#1698/#1681/#1682/#1683（infra/CI）、#1707 等科学算法域、数据/模型/实验域 P1/P2 与 2026-10-18 sweep 的非白名单模块项：OUT_OF_SCOPE（各属并行轨权属），不在本轨重复修复。

## 结论
- 唯一确认 LIVE 且为 C0x 指定复查项的实质缺陷：**R2-F3+R2-F6（TaskCenter 索引空间）**。
- 次级 LIVE（小、可验收、白名单内）：#1695-17e（批量输出原子性）、#1695-17b（i18n 复数）。
- 形式 UB 残留：#1646-2（atomic 化 m_running）——视预算与评审结论决定是否纳入。
