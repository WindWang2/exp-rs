# R21-C: Pi/MCP Harness、科学决策、Workflow 与插件工具契约 — fail-closed 加固与质量收敛

**Base**: `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（执行时 origin/master，gh api compare 双确认 identical）
**Head**: `02ce1622ee46448a819246fe3b81e53faa840a9f`（19 commits，独立 worktree `exp-rs-r21-C-20261023T185308Z`）
**PR**: https://github.com/WindWang2/exp-rs/pull/1675（OPEN，非草稿；不等待 CI、不自动合并）

## 读过的 PR / issues
- PR：#1670（R20-03 全部修复，反回滚守卫逐点复核在位）、#1666/#1668/#1669/#1671/#1672（R20 兄弟轨，只读）。Open PR=0。
- Issues（126 open 全量分诊，`.planning/r21-C/ISSUE_TRIAGE.md`）：本轨修复 **#1597（半 2）**、**#1595（残留）**、CT-B/D9；FIXED-OPEN 复测证据 #1551/#1549/#1588/#1595 主体/#1539/#1540/#1552/#1589/#1592/#1594/#1572；维持 DEBT #1628-3/#1629-1/#1630-2/#1596；跨轨不碰 #1598/#1599。

## 已完成工作包（全部 RED→GREEN，证据在 .planning/r21-C/RED_GREEN.md + TEST_RESULTS.md）

### 修复（src 9 文件）
1. **W-A [P2] #1597 半 2**：pipeline/workflow/catalog-export/data catalog search 的未知 flag 从"静默吞掉（exit 0 跑出不同结果）"改为 typed InvalidInput 四元组拒绝；含末位 flag 循环上界修复与缺值拒绝。
2. **W-B [P2] #1595 残留**：`--max-items` 裸 toInt()（垃圾→0）补 parse-ok 门；三个数值门全部携带四元组。
3. **W-D9 [P3] CT-B**：CLI 人工修复审批门从不可达变为可用——`--repair-approval <token.json>`（1MiB 有界读+isObject 门）+ `--approval-now-ms` + `--now-ms`/`--ttl-ms`（铸币半场）；裸 `--approve` typed 拒绝并指路；session 族支持全局 envelope flag（`--json`）。
4. **W-E1 [P1] F1**：`get_execution_status` 轮询结果回带 execution_id 曾把 rpc-id→task 取消映射武装——UI/桥对**轮询本身**的 abort 会误杀其仅报告的执行。终版为**轮询黑名单**（R2 加固：直连路由提交者的取消通道不丢）。
5. **W-E2 [P2] F2**：`preflight_algorithm`/`execute_algorithm`/`execute_operator` 的 `parameters` 非 object 曾被静默降级为 `{}`（preflight 对默认值报 ok=说谎；execute 以默认参数跑）→ typed INVALID_PARAMETER。
6. **W-E3 [P3] F3**：已终态 session 二次 `run()`（goal 不匹配）曾追加第二条冲突 terminal 事件到持久 journal（replay last-write-wins 翻转 delivered→refused）。
7. **W-E4 [P3] F4 (pi)**：wait 工具模型可控的 `poll_interval_ms` 0/负/非有限值 → 整个超时窗事件循环级忙轮询；钳制 [100ms,60s] 单源导出。
8. **D5 [P3]**：plugin_host_session 的 `mProcessHandle` 普通指针读点与 close 选举清除竞态 → `mLastKnownPid` write-once 快照；死后 diagnostics 仍点名 worker pid。
9. **D6 [P3]**：workflow_run_lock（64KiB）与 preflight capability_mirror（4MiB，按文件大小分配）的有界读。
10. **R1-2 [P1]**：`data catalog search --platform <value>` 曾被 Qt argv 解析吞掉并 **abort 进程**（exit 134 无 envelope）——仅从 Qt 视图摘除该 token，dispatch 走快照（Track-14 `--name` 同构）。

### 测试（tests 13 文件 + pi 1）
11. **W8：plugin 诊断码族 25 个"master 同构红"实为测试 UB**——`PluginRegistry::diagnostics()` 按值返回快照，13 处 range-for 迭代已析构临时（编译器警告早已指出）。R20-03 归因 fixture/环境；修复后 **5 二进制全绿**（exprs_plugin_loader 566/38、halffail 69/9、unload_order 34/4、boundary_r4 35/8、**host_process 371/34 含 sawTypedRefusal**、channel_parity 10/3）。
12. 新 RED→GREEN 测试覆盖全部修复；R2 补 F1 黑名单的 revert-guard（白名单下实证失败）。

## 回归 / 基准
- **47 个测试调用 ×2 遍全部退出码一致**（diff 实证），其中 40 个在跑内全绿 + 7 个补建目标全绿：mcp_server 9692/32、surface_e2e 4372/1、surface_parity 5484/12、exprs_ipc 176/29、capability_knowledge 1637/17、harness_evals 302/19、agent_ops_core 471/47、science_context_concurrency 10130/14、surface_protocol_r4 2609/11、plugin_host_process 371/34 等（全表见 TEST_RESULTS.md）。
- **R20 #1670 回归两遍绿**（exprs_ipc/capability_knowledge/harness_evals/agent_ops_core/surface_parity/surface_e2e/mcp_server 族）。
- **KNOWN_BASELINE_FAIL：0**（R20-03 的 plugin 5 二进制与 CT-6 之外全部转绿；CT-6 属 D 轨红区未重跑）。
- pi 套件 41/41；E2E 5 条真实二进制链路（discover→inspect→plan→数值/flag 契约→审批通道）全部实证。
- **假红归因**：harness_evals/exprs_ipc 首轮失败由 PROJ_DATA 缺失、surface_e2e 由 surface_mcp_host 未构建引起——环境修正后全绿（非代码问题）。
- **NOT_EXECUTED**：线上 CI（#1491 全仓失效）、Win/mac、EMBED_PYTHON 门控目标、Tsan/ASan/UBSan（Debug+锁内 -j2 优先功能验证；UB 修复有编译器警告+行为双重证据）。

## 构建配方（本主机现场 debugging 产出，详见 BASELINE.md）
vendor 前缀 + 系统 expat/zlib/zstd 覆盖（静态非 PIC 链入共享库失败）+ link-shim（vendor libgdal 的 26 个 `wrapper2_*` LAPACK 符号全主机无提供者，采用 R19-02/R21-B 既有 shim 模式）+ `LD_LIBRARY_PATH`/`PROJ_DATA`/`GDAL_DATA` 运行环境。R21-B 当前二进制同样无法直接运行，recipe 已文档化供各轨复用（CROSS_TRACK_FINDINGS XT-2）。

## Review
- **R1**（fresh 对抗）：READY-WITH-FIXES；1×P0（workflow resume 双重 takeFirst 致 SIGABRT——本轨引入的硬回归）+2×P1+2×P2+3×P3 全闭（REVIEW_R1.md）。
- **R2**（第二 fresh，全范围+实际执行+对抗探针）：READY-WITH-FIXES；1×P2（F1 黑名单缺 revert-guard，已补并实证 RED/GREEN）+3×P3 全闭。
- 两轮均实证：R20 #1670 四修复点原位在位零回滚；所有权 100% 合规。

## 剩余风险与未执行项
- **push/PR 阻塞**：github.com:443 网络分区（`git fetch/push` TLS 超时；`curl https://github.com` 超时；api.github.com 与 `gh api` 正常）。所有 commit 已在本地分支 `agent/r21-c-pi-mcp-harness-20261023T185308Z`（19 个）可直接 push；网络恢复后 `git push -u origin HEAD && gh pr create -R WindWang2/exp-rs --base master --head <branch> --body-file .planning/r21-C/PR_BODY.md` 即可。
- XT-1：共享 stash 误 pop 事故已完整恢复（CROSS_TRACK_FINDINGS），E 轨 owner 需认领 `stash@{0}`（RECOVERED-BY-R21-C）。
- P3 遗留（不影响正确性）：workflow resume 多余位置参数被忽略（与族规则一致）；`--now-ms/--ttl-ms` 对非 approve-repair 动作无害接受；test_preflight_provider 写 5MiB 临时文件（/tmp 满载时有假红风险，已归因）。

## 文件白名单（26 文件 + .planning/r21-C）
pi/{exp-rs-spatial,mcp_bridge}.ts + pi/test/r21_c_poll_interval_clamp.test.mjs；src/agent/mcp_server.cpp；src/agent_loop/scientific_agent_session.cpp；src/cli/{cli_agent_ops_commands,cli_commands,cli_commands.h,main_cli}.cpp(.h)；src/plugins/host/plugin_host_session.{h,cpp}；src/preflight/capability_mirror.cpp；src/workflow/workflow_run_lock.cpp；tests/ 13 个同名测试；.planning/r21-C/**（13 文档）+ .gitignore 4 行 append-only。
