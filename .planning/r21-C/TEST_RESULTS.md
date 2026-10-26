# R21-C TEST_RESULTS — 终版（2026-10-24）

## 环境与运行方式
- 构建：vendor 前缀 + expat/zlib/zstd 系统覆盖 + link-shim（wrapper2 LAPACK 桩）+ `-Wl,--allow-shlib-undefined`；配方见 BASELINE.md。
- 运行：`LD_LIBRARY_PATH=<build>/link-shim:<vendor-prefix>` + `QT_QPA_PLATFORM=offscreen` + `PROJ_DATA=<vendor>/share/proj` + `GDAL_DATA=<vendor>/share/gdal`。缺 PROJ_DATA 时 harness_evals/exprs_ipc 出现**假红**（已归因）。
- 全部经共享锁 `/home/kevin/project/exp-rs-build.lock` 串行（-j2）；测试 -j1。
- 二进制位置：`build-r21-C/tests/`（多数）与 `build-r21-C/`（plugin_host_process、lifecycle_*、boundary_r4、channel_parity_r4、r18_c、exprs_ipc、surface_mcp_host 等）。

## RED 证据（master src + HEAD 测试，同环境）
| 二进制 | RED | 归因 |
|---|---|---|
| test_cli_exit_codes_r4 | 5 败 | W-A（workflow validate/run、pipeline validate、data 未知 flag ×2；master 分别退 0/3/0/0/0） |
| test_cli_error_messages_r4 | 3 败 | W-A 四元组 + W-B `--max-items` 消息 |
| test_cli_agent_ops | 1 败 | W-D9（bare --approve master 退 5） |
| test_plugin_host_process | 2 败 | D5（:416 deadPid==0）+ 预存红（:1289 sawTypedRefusal，后证明为 UB） |
| test_workflow_run_lock_r4 | 1 败 | D6（pid 4242 被从超大种植文件挖出） |
| test_preflight_provider | 1 败 | D6（5MiB 种植报 "unparseable" 而非 "exceeds"） |
| test_r18_c_rpc_cancel_id_namespace | 1 败 | F1（cancel(99) 杀死被轮询的任务） |
| test_mcp_server | 6 败（3 section×2） | F2（string parameters 无 INVALID_PARAMETER） |
| test_agent_loop_modes | 1 败 | F3（replay 终态翻 refused） |
| pi/test/r21_c_poll_interval_clamp | 1 败（SyntaxError） | F4（master mcp_bridge 无导出） |
| test_exprs_plugin_loader / halffail_r4 / unload_order_r4 / boundary_r4 | 16/6/1/1 败 | **测试 UB**（见下 W8 归因） |

## GREEN ×2（HEAD 终态：全部修复 + R1/R2 审查修复，21 commits）
两遍完整运行（干净环境、无并发构建污染），47 个调用退出码逐一相同（diff 实证 identical）；42 个在跑内全绿，5 个 MISSING（位于 build 根目录）单独补跑全绿（下表 ★）。首轮复验曾因验证期间并发重建二进制而污染，已作废重跑——最终两遍为干净结果。

| 二进制 | 结果 |
|---|---|
| test_cli_exit_codes_r4 | 211/7 ✓ |
| test_cli_error_messages_r4 | 100/6 ✓ |
| test_cli_agent_ops | 57/5 ✓ |
| test_workflow_run_lock_r4 | 98/9 ✓ |
| test_preflight_provider | 177/17 ✓ |
| test_r18_c_rpc_cancel_id_namespace | 10/2 ✓ |
| test_mcp_server | 9691/32 ✓ |
| test_agent_loop_modes | 110/11 ✓ |
| test_exprs_ipc | 176/29 ✓ |
| test_capability_knowledge | 1637/17 ✓ |
| test_harness_evals | 302/19 ✓ |
| test_agent_ops_core | 471/47 ✓ |
| test_surface_parity | 5484/12 ✓ |
| test_surface_e2e | 4372/1 ✓ |
| test_plugin_host_process | 371/34 ✓ |
| test_exprs_plugin_loader | 566/38 ✓ |
| test_plugin_lifecycle_halffail_r4 | 69/9 ✓ |
| test_plugin_lifecycle_unload_order_r4 | 34/4 ✓ |
| test_plugin_host_boundary_r4 | 35/8 ✓ |
| test_plugin_channel_parity_r4 | 10/3 ✓ |
| test_plugin_host_allowlist / plugins_runtime_host / plugin_execution_barrier / plugin_manifest / exprs_plugin_system | 24/3、63/7、23/6、378/11、129/16 ✓ |
| test_plugin_capabilities | 129/14 ✓ |
| test_science_context_concurrency | 10130/14 ✓ |
| test_science_context_broker / live_authorities | 183/40、197/15 ✓ |
| test_surface_protocol / _r4 | 74/10、2609/11 ✓ |
| test_agent_ops_task_center_executor | 101/28 ✓ |
| test_workflow_cancel / r19_02_workflow_cancel_order / workflow_execution_plane / workflow_recovery | 74/8、1009/4、96/8、163/15 ✓ |
| test_agent_loop_seams | 101/10 ✓ |
| test_r14_C_mcp_workflow_cancel | 21/2 ✓ |
| test_preflight_authority_invalidation / check_tool / engine / rules | 294/20、37/4、70/17、481/18 ✓ |
| test_cli_command_surface / commands_json / help_vocabulary_r4 / fail_fast_r4 / batch_manifest | 27/3、53/9、277/4、34/4、105/11 ✓ |
| pi Node 套件（41 tests，含新增 F4 钳制） | 41/41 ✓（两轮之外另跑一次） |
| ★ test_science_context_broker / live_authorities | 183/40、197/15 ✓ |
| ★ test_plugin_capabilities | 129/14 ✓ |
| ★ test_cli_command_surface / help_vocabulary_r4 / fail_fast_r4 / batch_manifest | 27/3、277/4、34/4、105/11 ✓ |
| test_r18_c_rpc_cancel_id_namespace（含 R2 revert-guard） | 13/3 ✓ |

## W8-red 归因（R20-03 移交的 5 二进制"master 同构红"）
R20-03 记录 5 个 plugin 二进制共 25 个失败断言为 master==HEAD 同构、疑属 fixture/环境。**本轮实证根因：测试代码 UB** —— `PluginRegistry::diagnostics()` 按值返回快照（#1449 有意设计），而 13 处 `for (item : registry.diagnostics().items())` 迭代的是 range-init 语句结束时即析构的临时内的 vector（编译器 -Wdangling 警告早已指出）。本机 toolchain 下 UB 静默产生垃圾读数，所有诊断断言假失败。修复（命名常量绑定快照）后 5 二进制全绿（上表）。**结论：非环境问题、非本轨引入；R20-03 的 master 同构判断方向正确（src 无关）但归因（fixture 行为）错误。**

## KNOWN_BASELINE_FAIL / NOT_EXECUTED
- **KNOWN_BASELINE_FAIL：0**（R20-03 的 CT-6 surface_snapshot registry 腿本轨未重跑——属 D 轨红区；plugin 5 二进制已转绿）。
- **NOT_EXECUTED**：
  - 线上 CI（#1491，2026-10-11 起全仓失效）——本地全测替代；
  - Win/mac 构建（本机 Linux only）；
  - `SICNU_EMBED_PYTHON` 门控目标（test_plugin_host / test_python_plugin_host / test_python_plugin_manager，本配置 OFF 不存在）；
  - Tsan/ASan/UBSan 未跑（Debug 构建 + 锁内 -j2 优先保功能验证；UB 修复经编译器警告+行为双重实证）；
  - test_surface_snapshot（CT-6，D 轨红区，故意不碰）。
- **假红归因记录**：harness_evals/exprs_ipc/surface_e2e 首轮失败分别由 PROJ_DATA 缺失与 surface_mcp_host 未构建引起，环境修正后全绿（非代码问题）。

## E2E（真实二进制，M5）
1. discover：`algorithms list`（未知 flag 被既有契约拒绝）+ `algorithms schema rs:spectral_index` ✓
2. plan：`workflow validate`（valid:true）；拼错 flag → 完整四元组 typed 拒绝 ✓
3. 参数契约：`data catalog search --limit abc` / `--platfrm`（末位）/ `--limit`（缺值）→ 三条四元组拒绝 ✓
4. 审批通道：`session run --approve` → typed 拒绝并指路；`session run --repair-approval tok.json` → token 被接纳进入 driver（SEAMS_UNAVAILABLE 为 CLI 宿主无生产缝的预期下一站，证明 token 未被丢弃）✓
5. Pi 扩展：node 套件 41/41（桥接、取消、超时、快照三角防漂移）+ test_surface_e2e 真实 surface_mcp_host 4372 断言 ✓
