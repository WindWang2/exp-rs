# r25-C: Pi/MCP 空间智能 Harness、Workflow 执行与插件工具契约提升

## 归属
- Base SHA: `2610cb4741e69a89124921e44c03e340e2522ee2`（执行时 `origin/master`，与本 PR 推送前复查一致）
- Head SHA: `a5d0330611`（分支 `agent/r25-C-pi-mcp-execution-20261028T140339Z`，5 commits）
- Issue 归属：R25-C 专项。实时 triage（11 个候选 issue 逐条对照 master 生产代码）见 `.planning/zcode-r25-C/ISSUE_TRIAGE.md`：#1690/#1704/#1691/#1692/#1705 均为 FIXED_IN_MASTER_BUT_OPEN（回归钉维持，不重修）；#1654 的部分债务由本轨 W3 清偿；#1628/#1629/#1693 汇总型由代码交付；#1640/#1650 无最小复现，不虚构修复。

## 交付内容（W1–W3 三项功能深化 + 测试/E2E/安全）

### W1 — Pi 桥接生命周期诚实化（`pi/mcp_bridge.ts`、`pi/exp-rs-spatial.ts`）
- initialize 响应的 `serverInfo`/`protocolVersion` 此前被丢弃，respawn 后版本漂移不可观测。现在：`serverInfo`/`protocolVersion`/`generations` getter + `onGeneration` 回调（每次成功握手携带 prev/current serverInfo）。
- 扩展壳在 respawn 后重新 `tools/list` 比对目录漂移，通过 `exprs_status` 披露（Pi 无工具注销 API，故「检测+报告」而非假装重注册）。
- 所有 bridge 自建错误携带 `bridgeError` 类型码 + `retryable`（CHILD_EXITED / RESTART_FAILED / LAUNCH_FAILED / REQUEST_TIMEOUT / STREAM_DESYNC / PAYLOAD_TOO_LARGE / BRIDGE_STOPPED / ABORTED），Pi 无需解析英文散文即可分类重试——与 #645 工具级 errorCode/errorCategory 同一 rationale。
- R1/R2 修复：onGeneration 同步抛出与异步拒绝双遏制（否则 unhandledRejection 终止 Node 进程）；lazy-respawn 包装器保留 start() 失败的原错误类（不再把不可重试的 LAUNCH_FAILED 伪造成可重试）；**产品缺陷**：SIGKILL 升级计时器 `unref()` 后在静默事件循环中永不触发 → SIGTERM-ignoring 子进程成孤儿、长期持有 workflow run flock（实测使 resume 拒绝 "owned by a live process"）——计时器改为带监听器（child exit/error 即清除），process-exit hook 先 `killNow()` 再 `stop()`（R2 证明原顺序使 killNow 成为死代码）。

### W2 — 能力发现诚实化（`src/agent/mcp_server.cpp`）
- `AlgorithmDescriptor` 的 AgentMetadata 早已携带 `prerequisites`/`limitations`（如 `rs:spectral_index` 的波段数前置条件），但 list/search_algorithms 的条目投影全部丢弃。现在条目携带声明的 prerequisites/limitations + 紧凑 guarantees（deterministic/idempotent/supportsCancellation）；无声明则省略键（有界 token 成本，不输出空数组）；provider 条目保持同一形状（布尔键，缺省为 descriptor 真值）。

### W3 — Workflow 恢复诊断（`src/workflow/workflow_run_coordinator.{h,cpp}`、`src/agent/mcp_server.{h,cpp}`）
- 服务器重启后内存 pipeline map 消失，agent 只持有 run_id 时此前只能盲试 resume。`get_workflow_status` 新增 run_id 路径：checkpoint 真值（run state / 每步 plan 状态 / progress / 诚实的 `checkpoint_present` 探测 / 镜像 resumeRun 状态墙的 `resumable` 裁决 + resume 提示）。只读观察：不取 run lock、不改状态；resumeRun 保留所有权权威。
- R1 修复：`steps[].output_path` 经 `diagnosticArtifactPath()`——工作区内原样、区外坍缩为文件名（R2 修正了 workspace root 为 `/` 时 `root+"/"` 变 `"//"` 的误伤，改用 sandbox 自己的 `canonicalPathWithin`）；live pipeline 路径同样获得 resumable advisory（additive keys）。

### 测试与验证（本地实测，exit code 全部为 0）
- **RED→GREEN**：每个新测试均先在其修复缺失时失败（子代理 R1 在 `origin/master` 源码上复跑 W1 测试 4/4 RED；R2 要求判别性证明后，对 `\z`→`\Z` 与路径 pass-through 两次 revert 各产生 1 失败再恢复）。
- **GREEN×2**：
  - `node --test pi/test/` → 48 pass / 0 fail / 0 skip，两轮（含真二进制 E2E 五链：能力诚实发现 → TaskCenter 执行 → 类型化拒绝 → 取消 → 崩溃后 checkpoint 诊断 → resume 完成；11–17s/轮）。
  - C++：`test_r25_c_capability_honesty` 198、`test_r25_c_workflow_resume_diagnostics` 95、`test_mcp_server` 9962、`test_workflow_runtime` 223、`test_workflow_recovery` 163、`test_workflow_run_coordinator` 193、`test_workflow_crash_recovery_r4` 115、`test_r14_C_mcp_workflow_cancel` 21、`test_surface_snapshot` 17866、r22-agent 回归钉 78+46+7+17+24031 断言，全部两轮绿。
- **surface snapshot**：`get_workflow_status` schema 变更走正规再生（`surface_snapshot_regen`），diff 仅该工具；pi 侧 `surface_snapshot.test.mjs` 与 C++ 侧 `test_surface_snapshot` 双向钉住。
- **顺带修复**：`pi/test/r18_c_timeout_cancels_server_side.test.mjs` 的跨进程读写竞态（预存 flake， pristine master 2/8 全套件运行复现）以 bounded poll 修复；`r18_c_id_null_error_fanout` 的负载相关 flake 记录归属（单跑 0/25 不复现），未盲改。

### 独立评审（两个未参与实现的新上下文子代理）
- **R1**（`.planning/zcode-r25-C/REVIEW_R1.md`）：P0=0 P1=0 P2=2 P3=9 → 全部修复（commit `41425da645`）。
- **R2**（`.planning/zcode-r25-C/REVIEW_R2.md`）：P0=0 P1=0 P2=3 P3=7 → 全部修复（commit `a5d0330611`），含 killNow 死代码、`/` workspace 路径坍缩、判别性测试补齐（revert 实证）。

## 跨轨与共享文件
- `tests/CMakeLists.txt`：仅末尾追加 `# BEGIN R25-C`…`# END R25-C` 块（2 个测试目标）。`python scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs` → 1065 targets / 50 add_test，lost 0+0，added 2+0，exit 0。
- `.gitignore`：末尾追加本轨 planning allow-block（markdown only）。
- 与并行 PR #1724/#1725/#1726 的 changed-files 交集仅限上述两个 append-only 文件；无源码重叠。C/D 轨文件（cartography/mapspec/layout/symbology/app/ui/operators/core/gui）零触碰。

## 未执行 / 限制
- 未等待 GitHub CI；未自动合并。
- GPU/在线 STAC/TensorRT 等本轨不涉及；无平台特异性代码新增（pi 测试沿用既有 Windows shebang canary skip 约定）。
- 性能：变更均为 O(1)/O(steps) 投影与元数据字段，无新热点路径，未引入 wall-clock 门槛（`.planning/zcode-r25-C/PERF_BASELINE.md`）。

## 证据文件
`.planning/zcode-r25-C/`：BASELINE / GOAL / ISSUE_TRIAGE / PR_COLLISION_MATRIX / CROSS_TRACK_FINDINGS / TASKS / ORACLES / DECISIONS / RISK_REGISTER / LOOP_LEDGER / TEST_RESULTS / PERF_BASELINE / REVIEW_R1 / REVIEW_R2。
