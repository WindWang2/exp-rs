# R21-C FINAL_REPORT — Pi/MCP Harness、科学决策、Workflow 与插件工具契约

## 分支与基线
- **Branch**: `agent/r21-c-pi-mcp-harness-20261023T185308Z`（独立 worktree `/home/kevin/project/exp-rs-r21-C-20261023T185308Z`，BASE=911e0b17a3）
- **BASE**: `911e0b17a3533ca0d79c1b68b000d6052c4c3c61` = 执行时 origin/master（`gh api compare` 双确认 identical；github.com 直连分区，用 gh API 完成 LIVE 盘点）
- **21 commits**：3 RED 网 + 9 修复 + 1 UB 修复 + 5 R1 修复 + 1 R2 修复 + 2 文档
- **Head**: `8480611326`

## PR 状态
- **PR #1675: https://github.com/WindWang2/exp-rs/pull/1675**（OPEN，非草稿，22 commits，head=157c1fe079 → base=master 911e0b17a3；不等待 CI、不自动合并、不替用户合并）
- 提交过程中 push 曾遇网络分区（github.com:443 不可达，git fetch/push TLS 挂起；api.github.com/gh api 正常），后台重试循环第 1 次即恢复并成功推送，随后 gh pr create 成功。
- PR 正文：`.planning/r21-C/PR_BODY.md`（base/head SHA、读过的 PR/issues、工作包、回归/基准、review 修复次数、剩余风险、白名单）。

## 头条成果
1. **W8-red 归因翻转（本轨最大发现）**：R20-03 记录为"master 同构、疑 fixture/环境"的 plugin 诊断码族 **25 个失败断言实为测试代码 UB**——`PluginRegistry::diagnostics()` 按值返回快照（#1449 有意设计），13 处 `for (item : registry.diagnostics().items())` 迭代 range-init 语句结束时即析构的临时内的 vector（编译器 -Wdangling 警告早已指出，无人处理）。修复后 **5 个二进制从 25 败到全绿**（exprs_plugin_loader 566/38、halffail 69/9、unload_order 34/4、boundary_r4 35/8、**host_process 371/34 含 R20-03 记录的 sawTypedRefusal**、channel_parity 10/3）。
2. **R1 抓出并修复本轨引入的 P0**：W-A 的未知 flag 门把 run id 先从 args 取走而 runner 仍消费一个 token → `workflow resume run-x` 在空列表 takeFirst（Debug SIGABRT/134 无输出；release UB），`workflow resume a b` 续错 run。逃逸原因：resume 族零 CLI 覆盖。已修 + 补测试。
3. **F1 [P1] 轮询 abort 误杀执行**：status 轮询结果回带 execution_id 曾武装取消映射，UI/桥对轮询本身的 abort/超时会杀掉其仅报告的长任务。终版轮询黑名单（R2 加固：直连路由提交者取消通道不丢，含 revert-guard 测试实证）。
4. **F2 [P2] 说谎的 preflight**：parameters 非 object 被静默降级 {} → preflight 对默认值报 ok、execute 以默认参数跑。typed INVALID_PARAMETER。
5. **构建配方攻坚**（本主机现场 debugging，已文档化供五轨复用）：静态非 PIC expat/zlib/zstd 链入共享库失败 → `*_LIBRARY_RELEASE` 覆盖；vendor libgdal 的 26 个 `wrapper2_*` LAPACK 符号全主机无提供者 → link-shim 模式；运行期 DT_RUNPATH 不传递 → LD_LIBRARY_PATH + PROJ_DATA/GDAL_DATA（缺 PROJ_DATA 曾致 harness_evals/exprs_ipc **假红**，已归因）。
6. **--platform abort 修复（P1）**：`data catalog search --platform sentinel-2` 曾被 Qt argv 解析吞掉并 abort 进程（exit 134 无 envelope）；仅从 Qt 视图摘除 token，dispatch 走快照（Track-14 `--name` 同构）。实证 ok:true。

## 数字
- src 9 文件（+~560/−~45）+ tests 13 文件（+~470）+ pi 3 文件 + .planning/r21-C 13 文档 + .gitignore 4 行
- RED：9 个工作包在 master src + HEAD tests 同环境全部实证失败（exit_codes 5 败、error_messages 3、agent_ops 1、plugin_host_process 2（D5+预存）、run_lock 1、preflight_provider 1、r18_c 1、mcp_server 6、agent_loop_modes 1、pi SyntaxError）
- **GREEN ×2（干净环境，47 调用退出码两遍 diff identical）**：42 跑内全绿 + 7 根目录目标补跑全绿 = **49/49**；pi 41/41；E2E 5 条真实二进制链路
- **KNOWN_BASELINE_FAIL：0**；假红 2 类已归因（PROJ_DATA / surface_mcp_host 未构建）
- Review：R1 8 发现（1 P0）全闭；R2 5 发现（1 P2）全闭；R20 #1670 四修复点零回滚（git log 空 diff 实证）

## 测试矩阵（终值，全表见 TEST_RESULTS.md）
mcp_server 9692/32、surface_e2e 4372/1、surface_parity 5484/12、exprs_ipc 176/29、capability_knowledge 1637/17、harness_evals 302/19、agent_ops_core 471/47、science_context_concurrency 10130/14、surface_protocol_r4 2609/11、plugin_host_process 371/34、exprs_plugin_loader 566/38、cli_exit_codes_r4 226/7、cli_agent_ops 61/5、workflow_recovery 163/15、r19_02_workflow_cancel_order 1009/4、preflight_rules 481/18 等 49 项。

## Oracle checklist
| Oracle | 状态 |
|---|---|
| O-C1 词汇表一致 + R20 #1670 回归两遍绿 | ✅（exprs_ipc/capability_knowledge/harness_evals/agent_ops_core/surface_parity/surface_e2e/mcp_server 两遍同值） |
| O-C2 fail-closed（授权撤回/取消重入/资产变化） | ✅（F1 轮询取消门+revert-guard；既有 cancel/recovery 族 1009/163 断言两遍绿） |
| O-C3 Workflow placeholder 单次替换 + resume 不重复 publish | ✅（既有回归两遍绿；resume P0 已修并有新覆盖） |
| O-C4 malformed 输入 typed errors 宿主不 abort | ✅（W-A/W-B 四元组；--platform abort 修复；#1551 回归） |
| O-C5 真 E2E ≥4 条两轮 + sidecar diff | ✅（5 条真实二进制链路；pi 41/41；surface_e2e 4372 断言） |
| O-G1 盘点可查 | ✅（BASELINE/ISSUE_TRIAGE/PR_COLLISION_MATRIX） |
| O-G2 每项修复 RED/GREEN ×2 + 生产路径 | ✅（RED_GREEN.md；F4/F1 有 SyntaxError/白名单回退双重实证） |
| O-G3 性能基准与内存上界 | ✅（PERF_BASELINE.md；D6 从 O(file) 降到 O(cap)，mirror 按大小分配） |
| O-G4 受影响测试 + 两个独立 reviewer PASS | ✅（R1 fresh + R2 fresh，各自动工 executions） |
| O-G5 known-red/skip/not-executed 逐项归因 | ✅（TEST_RESULTS.md：KNOWN_BASELINE_FAIL=0；NOT_EXECUTED 五项逐项归因；假红两类） |
| O-G6 PR 文件 100% 在所有权内 + 无静默回滚 | ✅（26 文件全在写区；R20 修复点 git log 空 diff） |
| O-G7 无 TODO 假实现/占位 mock/绕过门 | ✅（零 mock 替生产；所有断言对真实二进制/真实 driver） |

## 未完成项与移交
1. **push/PR**：网络分区阻塞（见上）；本地 21 commits 就绪。
2. **XT-1 共享 stash 误 pop 事故**：已完整恢复对方轨改动（内容零丢失，`stash@{0}: RECOVERED-BY-R21-C`），E/verification 轨 owner 需认领应用。各轨应被告知：`git stash` 视图跨 worktree 共享，pop 前必须按 hash 精确指定。
3. **XT-2 构建 recipe**：R21-B 二进制当前同样无法直接运行（libgeos 解析）， recipe 在 BASELINE.md 可复用。
4. **DEBT 维持**：#1628-3（phenology 接线=新功能面）、#1629-1、#1630-2、#1596（纯重构，W-A 已统一缺陷面）。
5. **P3 遗留**：workflow resume 多余位置参数被忽略（行为正确）；`--now-ms/--ttl-ms` 动作无关接受（无害）；test_preflight_provider 5MiB 临时文件（/tmp 满载假红风险已归因）。
6. **可关闭 issue 建议**（可核验，不擅自关闭）：#1597（剩余半边已修）、#1595（残留已修）；#1551/#1549/#1588/#1595 主体/#1539/#1540/#1552/#1589/#1592/#1594/#1572 为 fixed-open（证据在 ISSUE_TRIAGE.md）。

## 恢复点（下一批迭代）
`.planning/r21-C/{BASELINE,ISSUE_TRIAGE,OWNERSHIP,PR_COLLISION_MATRIX,RISK_REGISTER→CROSS_TRACK_FINDINGS,TASKS,ORACLES,RED_GREEN,TEST_RESULTS,PERF_BASELINE,LOOP_LEDGER,REVIEW_R1,REVIEW_R2,PR_BODY,FINAL_REPORT}.md` + 分支 `agent/r21-c-pi-mcp-harness-20261023T185308Z`（21 commits）。
