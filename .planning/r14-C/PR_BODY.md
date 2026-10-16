# hardening(r14-C): Pi/MCP Agent Harness、工具调用与 Workflow 执行闭环稳定性

## Scope / Ownership

Track **R14-C**（Pi↔exp-rs agent 栈收敛，无新产品方向）。生产代码改动限于：
`src/agent/mcp_server.{h,cpp}`、`src/workflow/pipeline_run_coordinator.{h,cpp}`、`src/agent_ops/operations_coordinator.cpp`、`src/plugins/framework/{data_provider_registry,plugin_runtime_host}.cpp`、`src/sdk/exprs/{external_process,ipc_channel,ipc_frame}.{h,cpp}`、`pi/mcp_bridge.ts`；测试为 4 个新 `test_r14_C_*` 二进制 + `test_agent_ops_core.cpp` 一个用例 + `pi/test/mcp_bridge.test.mjs` 两个用例 + `tests/CMakeLists.txt`/`.gitignore` 小块登记。

- **Base SHA:** `f72f0c11731361488a384a085797aae64efaf7ee`（origin/master，2026-10-15 fetch 复核）
- **Open PR 去重:** #1504（geodata，无交集）；#1505（r13-j）占用 `src/python/isolated/python_worker_process_pool.{cpp,h}` + `python_plugin_adapter.cpp` —— 本轨对这些文件只读核对、未改动（#1486 相关修复归 #1505）。
- **已合并 PR 去重:** `git show --stat` 证实 #1477/#1480/#1484/#1500/#1501 未触及 mcp_server / mcp_bridge / tool_catalog，本轨无重复修复；#1459/#1463/#1493/#1454/#1460/#1447/#1462/#1450/#1448(Engine-1) 逐条按提交定位为已修复（证据矩阵见 `.planning/r14-C/ISSUE_REVIEW_MATRIX.md`）。
- 主 checkout master 只读；独立 worktree `exp-rs-r14-C-pi-mcp-worktree`；与其他并行轨（A/B/D/E）共用 `/tmp/exp-rs-r14-global-build.lock` 串行构建（-j2）。

## 修复清单（RED → GREEN×2）

<!-- TEST_RESULTS_PLACEHOLDER：由真实运行结果填充 -->

| Fix | 缺陷 | 严重度 | 修复 |
|---|---|---|---|
| FIX-1 | `run_workflow`/`resume_workflow` 无法服务端取消：cancel 映射只认 `execution_id` 结果，Pi abort 后 pipeline 继续烧满预算（桥接层已按契约发送 `notifications/cancelled`） | P1 | `mcp_server`：rpc-id→pipeline-id 有界映射（仅提交类调用武装，状态轮询永不武装）；取消走 `WorkflowRunCoordinator::cancelRun`（与 GUI 同路径，含无可取消任务的 finalize）+ TaskCenter 兜底 |
| FIX-2 | #1448 Engine-2/D17 残留：terminal checkpoint 发布失败被吞掉后仍无条件释放跨进程 flock，peer 可对非持久终态 double-own | P2 | `persistCheckpoint` 返回 bool；terminal 发布失败时保留 flock 至协调器生命期（对齐 Engine-1 d5ab7bc0b 契约），qWarning 留痕 |
| FIX-3 | `OperationsCoordinator::run` 直接 `repairApproval["digest"].asString()`：畸形 token 抛 `Json::LogicError`；GUI dock 工作线程直调 `run()` 时即 `std::terminate`（#1447 同类新位点） | P2 | 与 `armRepairApproval` 相同的 #1451 先检查后取值守卫 → typed `APPROVAL_MALFORMED` |
| FIX-4 | `pi/mcp_bridge.ts`：(a) `fastCrashCount` 在健康 initialize 后不复位，5 次散布的 <10s 崩溃累计后整个会话永久禁用重拉起；(b) 出站无 4MiB 上限，一次超大调用触发服务端 `id:null` -32700 误杀全部 in-flight（含 10 路并发轮询） | P2 | 成功握手后电路归零；`MAX_OUTBOUND_LINE_CHARS`（镜像服务端 `kMaxMcpLine`）预检，仅该请求 typed 失败 |
| FIX-5 | `DataProviderRegistry`：插件 B 可静默替换 A 的 provider 并改写所有权（#1462 operator 劫持的数据侧兄弟） | P2 | 跨插件 first-registration-wins；owner 热重载仍允许 |
| FIX-6 | manifest/binary 两条路径都可覆盖 **builtin** 算子（`rs:ndvi` 类；RSOperatorRegistry last-write-wins，卸载插件还会删掉 builtin 注册） | P2 | 两个 created 分支加守卫：registry 已有该 id 而 host 无记账 ⇒ 非 plugin 注册 ⇒ 拒绝 |
| FIX-7 | `external_process`：(a) 显式 env 无 loader 注入 deny-list（manifest 可设 `LD_PRELOAD`）；(b) BSD `execvpeCompat` 从 child envp 取 PATH，probe/exec 分歧（Linux `execvpe(3)` 语义为调用者环境） | P2 | typed `env_injection (E5007)` 拒绝（大小写不敏感，LD_/DYLD_ 注入类）；compat 改用调用者 PATH |
| FIX-8b | `sendEnvelope` 以错误消息子串分类 E6003（#1459 点名的脆弱模式残留） | P3 | `writeJson` 增加 typed `failureCode` 出参，按码分类 |

## BLOCKED_WITH_EVIDENCE / 遗留项

- **FIX-8a**：`scientific:agent_session` 列出但在 `--mcp` 下永远 `AGENT_OPS_UNAVAILABLE`（`setAgentOpsDriver` 无任何调用方；`src/app/main.cpp:266-332` 未布线）。诚实修复需在 D 轨的 `src/app/main.cpp` 布线生产 driver，或做波及 4 层 parity/snapshot 套件的状态化重列表——留给 owner 轨，本 PR 只记录证据。
- Catalog 晚注册 staleness（P2）、journal 字节级上限（P3）、repair planner live-availability（P3）、`dispatchAndAwait` 阻塞 MCP 循环保留 30min（P2 latent）、`TaskCenterExecutor::poll` 对已剪枝任务误报 FAILED（P3）：均已记录于 ORACLES.md，fail-closed 或当前不可达。
- WP15 性能：HEAD 本无 MCP 面壁钟基准，本轮未新增；逻辑规模门禁已存在（见 ORACLES.md WP15）。未宣称任何未实测的性能收益。

## 验证（2026-10-15，build-r14-C：Ninja/Debug/-j2 + 全局 flock，ctest -j1，QT_QPA_PLATFORM=offscreen）

**RED（修复前实测）**——`git stash push -- src/` 后重建测试目标逐项运行：
- FIX-7：`REQUIRE_FALSE(result.started)` → `!true`（LD_PRELOAD 子进程照常 spawn；对照用例通过）
- FIX-1：`test_r14_C_mcp_workflow_cancel.cpp:261`、`:344` FAILED（取消通知无效，pipeline 继续跑满）
- FIX-2：`test_r14_C_d17_terminal_persist.cpp:154` FAILED（peer resume 成功——所有权已被错误释放；对照通过）
- FIX-5/6：`test_r14_C_plugin_registration.cpp:190`、`:219` FAILED（劫持/影射被静默接受）
- FIX-3：`test_agent_ops_core.cpp:1133` FAILED（`Json::LogicError` 逃逸）
- FIX-4：pi 两个用例——「Missing expected rejection」+ 真实生产故障文案「keeps crashing (5 fast crashes)」
- FIX-4 返工判别：sed 换回 `.length` → 字节测试失败（16 tests 1 fail）；恢复 `Buffer.byteLength` → 16/16

**GREEN×2**——修复恢复、重建后：
- `ctest -R 'R14-C FIX|malformed driver-held repair approval'`：**10/10 × 两次**（run1 5.94s / run2 3.36s）
- `node --test pi/test/`：**35/35**（含 3 个新用例）

**邻接回归（终树）**：test_mcp_server **30/30**（9670 断言，含 FIX-9）、test_agent_ops_core 47/47、test_exprs_ipc 28/28、test_exprs_external_process 13/13、test_plugins_runtime_host 7/7、test_surface_protocol_r4 11/11、test_workflow_checkpoint_cache 45/45、test_d17_workflow_pipeline_e2e 9/9。

**FIX-9（新发现，master 既有缺陷）**：`test_mcp_server`「recording handshake › prepare-stage refusal」在**未修改基线上确定性失败**（stash src/ 后重建运行两次均 29/30 同一断言）——`checkpointCount()==1` 与归档 sweep 竞态（快机上 bound_run 完成后 live+history 双副本=2）。本轨以时序无关断言修复（拒绝的 rebind 探针使用独立 pipeline id，断言其 checkpoint 不存在 + b.db 未创建）。共享测试文件短租已在 ledger 记录。

## Review

独立 Reviewer 两轮结论见 `.planning/r14-C/REVIEW.md`：R1 P0=0/P1=0（P2×2 已返工：出站上限字节化 + ORACLES 证据如实化；P3×9 记录留存）；R2 终树复跑签署。

## 不等待 CI 的说明

按轨道规约：本地定向构建/测试 + 独立审查为验收门禁；线上 GitHub Actions 结果不作为阻塞条件，也不修改 CI。
