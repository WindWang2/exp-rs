# BASELINE — R25-C (Pi/MCP 空间智能 Harness、Workflow 执行与插件工具契约提升)

- 执行时间：2026-10-28T14:03Z（实时 `git fetch origin --prune` 后记录）
- 基线：`origin/master` = `2610cb4741e69a89124921e44c03e340e2522ee2`（= PR #1723 merge commit）
- Worktree：`/home/kevin/projects/rs-studio/exp-rs-r25-C-20261028T140339Z`
- 分支：`agent/r25-C-pi-mcp-execution-20261028T140339Z`
- 源 worktree（`main`）状态：clean（仅 untracked `.workbuddy/`），未在其上开发。

## 远程事实（执行时实时查询）
- Open PR：**0**（`gh pr list --state open` → `[]`）。
- Open Issues：144；任务书候选 #1628/#1629/#1640/#1650/#1654/#1690/#1691/#1692/#1693/#1704/#1705 全部仍 OPEN（labels 见 ISSUE_TRIAGE.md）。
- 远程分支 9 条：master + agent/r23-{a..e} + agent/r24_{a,b,c}，均为已合并 PR 的遗留分支。
- 并行轨：R25-A（`exp-rs-r25-A-20261028T140339Z`）、R25-B（`exp-rs-r25-B-20261028T140313Z`）worktree 同期存在 → tests/CMakeLists.txt 与 .gitignore 必须严格末尾追加 union 协议；构建共用 `~/.cache/exp-rs-build.lock` flock（-j2 上限）。

## 上轮已交付（不得重复）
- #1714（r22-agent 轨）：MCP 沙箱 GDAL driver-prefix 逃逸、ExternalProcess env-key `=` 绕过、Python ghost 算法注册、IPC >1MiB 行丢弃、JobEngine logLines 无界。回归钉：`tests/test_r22_agent_{containment,env_keys,ghost_algo,ipc,job_logs}.cpp`。
- #1720（R24-C）：workspace-state charts 接 ChartRegistry、pi suite Windows 诚实 skip、#1699 i18n %n、#1695-3 discovery 线程化；surface snapshot re-pin。
- #1721（R23-C）：TaskCenter cumulative/delta 日志绝对索引。
- #1722/#1723（R23-A/B）：science algorithms / data-model runtime。

## 构建基线
- preset `dev-default` → `build-dev/`，`CMAKE_BUILD_PARALLEL_LEVEL=2`，ccache 跨 worktree 共享（base_dir=/home/kevin/projects/rs-studio，命中率 ~48%）。
- 基线构建目标：sicnu_geo_rs + test_mcp_server + r22_agent 5 项 + test_workflow_runtime（后台，log /tmp/r25c_build_baseline.log）。

## 关键读码结论（W0）
1. `pi/mcp_bridge.ts`（558 行）：传输成熟（generation 隔离、circuit breaker、desync kill、outbound 4MiB guard、abort→notifications/cancelled）。**缺口**：(a) initialize 响应的 serverInfo/protocolVersion 被丢弃 → respawn 后版本漂移不可检测；(b) 崩溃后 lazy-respawn 不重新 tools/list → 工具目录漂移不可见、Pi 注册陈旧；(c) bridge 级失败（child exit/timeout/desync/restart-failed/stopped）是裸 Error，无类型码（工具级错误有 errorCode/errorCategory，#645 已修，bridge 级没有）。
2. `src/agent/mcp_server.cpp`（3186 行）：handleGetWorkflowStatus 只按 pipeline_id 查询；服务器重启后 pipeline 注册表消失 → agent 持有 run_id 却无诊断面（不知道 run 是否可 resume、checkpoint 在哪一步）。resume_workflow 存在但无「可恢复性」探查。**缺口**：恢复诊断。
3. `algorithmEntryMap`：descriptor 有 `prerequisites`/`limitations`/`deterministic`/`idempotent`/`supportsCancellation` 字段（AgentMetadata 2.0），但 list/search 输出全部丢弃 → agent 看不到必需前置条件。**缺口**：能力诚实化。
4. custom_tools 门控一致（surface_registry + isToolIdAllowed 双重默认拒绝，带可操作 reason）。
5. WorkflowRunCoordinator：resume 机制成熟（flock 跨进程、terminal 状态墙、finalize in-flight 有界等待）；`report.interruptedRuns` 存在于 auto-resume 扫描，但无 MCP 查询面。

## 测试基线
- 待构建完成后首跑：test_mcp_server、r22_agent×5、test_workflow_runtime（记录通过数，作为邻域回归基线）。
- pi/test：node --test 直跑（无构建依赖）。
