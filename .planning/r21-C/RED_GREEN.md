# R21-C RED_GREEN — 每项修复的 RED→GREEN 证据索引

格式：工作包 | RED 证据（master src + HEAD tests） | GREEN×2 | 生产路径说明。
执行细节随轮次追加；最终数据见 TEST_RESULTS.md。

| 包 | 内容 | RED 载体 | 状态 |
|---|---|---|---|
| W-A | #1597 半 2：未知 flag typed 拒绝 | test_cli_exit_codes_r4 新 TEST_CASE 6 section | **RED 实证**（master：5/6 section 退 0 或 3；--max-items 经 validate() 巧合退 6，弱 RED→message 级强 RED 在 error_messages） |
| W-B | #1595 残留 --max-items ok 门 | 同上 + test_cli_error_messages_r4 message 断言 | **RED 实证**（error_messages:3 败含 "--max-items" 消息缺失） |
| W-D9 | CLI repair-approval 通道 | test_cli_agent_ops 新 TEST_CASE | **RED 实证**（1 败：--approve master 走 SEAMS_UNAVAILABLE(5) 非 InvalidInput(6)） |
| D5 | workerPid 死后归零 | test_plugin_host_process 新 TEST_CASE :416 | **RED 实证**（deadPid==0；另一败 :1289 sawTypedRefusal 为 R20-03 记录预存红，master 同构） |
| D6 | run_lock/mirror 无界读 | test_workflow_run_lock_r4（1 败：pid 4242 被挖出）+ test_preflight_provider（1 败："unparseable" 非 "exceeds"） | **RED 实证** |
| F1 [P1] | status 轮询 cancel 误杀任务 | test_r18_c_rpc_cancel_id_namespace 新 TEST_CASE | **RED 实证**（1 败） |
| F2 [P2] | parameters 非 object 静默 {} | test_mcp_server 新 TEST_CASE 3 section×2 断言 | **RED 实证**（6 败全属 F2） |
| F3 [P3] | 终态 session 二次 run 毒化 journal | test_agent_loop_modes 新 TEST_CASE | **RED 实证**（1 败：replay 翻 refused） |
| F4 [P3] | pi poll_interval_ms 钳制 | pi/test/r21_c_poll_interval_clamp.test.mjs | **RED 实证**（master 无导出→SyntaxError）+ **GREEN 已实证**（2/2 + 全套 41/41） |

## R20 #1670 守护回归（master src + HEAD tests，同环境，全绿——反回滚证据）
exprs_ipc 176/29、capability_knowledge 1637/17、harness_evals 305/19、agent_ops_core 471/47、surface_parity 5484/12、surface_e2e 4002/1（surface_mcp_host 补建后；PROJ_DATA/GDAL_DATA/LD_LIBRARY_PATH 环境修正后三项假红转绿）。

## 修复实现状态
- W-A/W-B/W-D9/D5/D6 修复已写、已过 -fsyntax-only，暂存于 git stash `r21c-src-fixes`（等 RED 实证后 pop+commit）。
- F1/F2/F3 修复形态已定（mcp_server cancel 门/parameters 门；session refuse/abort 终态诚实化），等 RED 构建后写码。
- F4 已交付（commit 44002ab676）。
