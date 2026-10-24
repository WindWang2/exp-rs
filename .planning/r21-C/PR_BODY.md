# R21-C: Pi/MCP Harness、科学决策、Workflow 与插件工具契约 — 质量收敛与 fail-closed 加固

**Base**: `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（执行时 origin/master，gh api 双确认 identical）
**Head**: `<待 push 后回填>`
**Worktree**: `exp-rs-r21-C-20261023T185308Z`（独立 worktree，五轨共享构建锁串行）

## 读过的 PR / issues
- PR：#1670（R20-03 全部修复反回滚守卫）、#1666（R19-02）、#1672/#1669/#1671/#1668（R20 兄弟轨，只读）。
- Issues（live 分诊见 `.planning/r21-C/ISSUE_TRIAGE.md`）：本轨修复 #1597（半 2）、#1595（残留）、CT-B/D9；FIXED-OPEN 复测证据 #1551/#1549/#1588/#1595 主体/#1539/#1540/#1552/#1589/#1592/#1594/#1572/#1570-1574；维持 DEBT #1628-3/#1629-1/#1630-2/#1596；跨轨不碰 #1598/#1599。

## 已完成工作包（全部 RED→GREEN，见 .planning/r21-C/RED_GREEN.md + TEST_RESULTS.md）
1. **W-A [P2] #1597 半 2**：pipeline run/validate/resume、workflow run/validate/list-runs/resume、catalog export、data catalog search 的未知 flag 从"静默吞掉（exit 0 跑出不同结果）"改为 typed InvalidInput 四元组拒绝（含末位 flag 循环上界漏访修复）。
2. **W-B [P2] #1595 残留**：`--max-items` 裸 toInt()（垃圾→0）补 parse-ok 门。
3. **W-D9 [P3] CT-B**：CLI 修复审批门从不可达变为可用通道——`--repair-approval <token.json>`（1MiB 有界读+isObject 门）+ `--approval-now-ms`；裸 `--approve`（全库零消费者的静默 no-op）改为 typed 拒绝并指路。
4. **W-E1 [P1] F1**：`get_execution_status`/`cancel_execution` 轮询结果回带 execution_id 曾把 rpc-id→task 取消映射武装起来——UI/桥对**轮询本身**的 abort 会误杀其仅报告的执行。任务版镜像 R14-C FIX-1 的 pipeline 提交门。
5. **W-E2 [P2] F2**：`preflight_algorithm`/`execute_algorithm`/`execute_operator` 的 `parameters` 非 object（字符串/数组）曾被静默降级为 `{}`——preflight 对默认值报 ok（说谎）、execute 以默认参数跑。改为 typed INVALID_PARAMETER（镜像 run_workflow 的 pipeline 门）。
6. **W-E3 [P3] F3**：已终态 session 二次 `run()`（goal 不匹配）曾追加第二条冲突 terminal 事件到持久 journal（replay last-write-wins 翻转 delivered→refused）。terminate 失败时不再记 terminal note。
7. **W-E4 [P3] F4 (pi)**：wait 工具模型可控的 `poll_interval_ms` 0/负/非有限值 → setTimeout 未钳制 → 整个超时窗事件循环级忙轮询（deadline 早退也永不触发）。钳制 [100ms,60s] 导出单源。
8. **D5 [P3]**：plugin_host_session 的 `mProcessHandle` 普通指针读点（workerPid/kill 选举败者/EOF probe）与 close 选举的清除竞态——镜像 #1644 `mLastKnownGroup` 的 write-once `mLastKnownPid` 快照；死后 diagnostics 仍如实点名 worker pid。
9. **D6 [P3]**：workflow_run_lock（64KiB）与 preflight capability_mirror（4MiB）的无界文件读加界——种植超大文件按名 typed 拒绝，不再整体缓冲。
10. **W8-red**：plugin 诊断码族 5 二进制 master 同构红——重跑归因与处置见 TEST_RESULTS（视结果回填）。

## 回归 / 基准
- R20 #1670 全部既有回归两遍绿（exprs_ipc/capability_knowledge/harness_evals/agent_ops_core/surface_parity/surface_e2e/mcp_server 族……见 TEST_RESULTS.md）。
- pi 套件 41/41（新增 F4 钳制测试后）。
- <GREEN×2 汇总回填>

## Review
- R1（fresh）：READY-WITH-FIXES，n×P0/P1/P2 全闭（见 REVIEW_R1.md）。
- R2（第二 fresh，含实际执行）：READY，零未解决 P0/P1/P2（见 REVIEW_R2.md）。

## 剩余风险与未执行项
- <回填：KNOWN_BASELINE_FAIL（CT-6 跨轨）、平台未执行项、环境差异>

## 文件白名单
src/cli/{cli_commands,cli_agent_ops_commands}.cpp；src/agent/mcp_server.cpp；src/agent_loop/scientific_agent_session.cpp；src/plugins/host/plugin_host_session.{h,cpp}；src/workflow/workflow_run_lock.cpp；src/preflight/capability_mirror.cpp；pi/{mcp_bridge,exp-rs-spatial}.ts；对应 tests/ 同名测试；.planning/r21-C/**。
