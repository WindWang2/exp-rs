# REVIEW_R1 — R25-C（独立评审，非作者）

- 评审对象：`git diff origin/master...HEAD`（worktree `/home/kevin/projects/rs-studio/exp-rs-r25-C-20261028T140339Z`，分支 `agent/r25-C-pi-mcp-execution-20261028T140339Z`，3 commits ahead of `2610cb4741`）
- 评审人上下文：未参与本轨实现；仅读码 + 允许的小型 JS 测试复跑 + 沙箱 RED 复现
- 未执行构建（按评审约束）；C++ 侧结论来自逐行读码与既有调用点比对

## 已执行的验证

| 验证 | 命令 | 结果 |
|---|---|---|
| 新 JS 契约测试 GREEN | `cd pi && node --test test/r25_c_generation_drift.test.mjs` | 4/4 pass |
| 全套件回归 | `cd pi && node --test "test/*.test.mjs"` | **46 pass / 0 fail / 0 skip** |
| RED 判别力（把 `origin/master:pi/mcp_bridge.ts` 放进沙箱副本再跑新测试） | `/tmp/r25c-redcheck` 沙箱，`node --test test/r25_c_generation_drift.test.mjs` | **4/4 FAIL**（getter 不存在 → `undefined`；`bridgeError` → `undefined`；onGeneration 事件数 0≠1） |
| 轨道边界 | `git diff origin/master...HEAD --name-only` | 未触碰 `src/{app,ui,operators,core,gui}/`、`src/agent/{cartography,mapspec,layout_tools,symbology}/` |
| snapshot 一致性 | diff 与 `meta_protocol_tools.cpp` schema 变更逐字段比对 + pi `test_snapshot` 通过 | 一致（`pipeline_id` 降为可选、`run_id` 新增、`required` 移除） |
| 测试真值来源 | `src/operators/rs/rs_spectral_index_operator.cpp:118`、`src/operators/opencv/opencv_filter_operators.cpp:77` | 断言的前置条件/限制均为真实声明，非虚构 |
| E2E 形状核对 | `tests/surface_mcp_host_main.cpp`（`rs:surface_noop`/`rs:surface_sleep`/`slices`）、`workflow_definition.cpp:163`（`operator`/`params`/`inputs`）、`mcpStatusForTask`（小写 `running`） | E2E 假设与生产形状一致 |

## 逐项结论（先说好的）

1. **`runForRunIdReadOnly()` 的锁纪律正确**（`workflow_run_coordinator.cpp:1784-1817`）：`shared_ptr` 在 `m_mutex` 内拷贝后返回（对象不会被并发擦除释放）；`loadCheckpoint` 在**不持有** `m_mutex` 的情况下调用，与 `resumeRunImpl:1219` 的 #944 纪律一致；`saveCheckpoint` 走 sidecar O_EXCL + 原子 rename，并发只读要么旧要么新，注释里 "consistent though possibly stale" 的断言成立。`hasCheckpointFor()` 单次取锁、不嵌套，无 flock↔m_mutex 反向序。
2. **`resumable` advisory 与 `resumeRun` 状态墙逐条对应**：`Interrupted/Failed/Canceled` + 非空 `definition().steps`（对照 `resumeRunImpl:1252-1281`）；active 状态明确把所有权裁决留给 `resumeRun`（`mcp_server.cpp:3193-3196`），没有谎报 `resumable=true`。
3. **`run_id` 守卫与 `handleResumeWorkflow:3251` 字节级一致**，且位于 `checkpointPathFor` 的路径插值之前；该分支只是已放行工具 `get_workflow_status` 的子路径，未新增权限旁路。
4. **bridge 错误类型化没有重打服务端错误**（`onLine:535-541` 保留 `code`/`data`、不附 `bridgeError`），与 DECISIONS #2 一致；`generationsValue` 只在握手成功后自增，注释与实现相符。
5. **JS 测试判别力已实证**：见上表 RED 行——4/4 在 master 代码上失败，非恒真断言。
6. `algorithmEntryMap` 新增键为纯增量，无既有测试做精确键集断言（`test_capability_surface_parity.cpp` 只比对 id 集合与 `catalog` 块存在性），回归风险低。

---

## Findings

### P2-1 `onGeneration` 的 try/catch 覆盖不到"返回已拒绝 promise"的回调，未处理拒绝会杀掉 Pi 进程
- 文件：`pi/mcp_bridge.ts:400-409`
- 问题：`this.onGeneration?.(generationInfo)` 只包了同步 throw；若回调**返回**一个 rejected promise（该钩子是导出 API，签名只写 `(info) => void`，没有约束必须同步），Node 默认 `--unhandled-rejections=throw` 会以未捕获异常终止进程。作者显然考虑过同步抛错（专门包了 try/catch 并打日志），但漏了异步拒绝这一支。
- 复现：在沙箱里给 `onGeneration` 传 `() => Promise.reject(new Error("x"))`，`node --test` 下整个测试进程以 unhandled rejection 退出；当前 shell 的回调内部自带 `.catch()` 所以暂未触发（latent）。
- 建议：`const r = this.onGeneration?.(info) as unknown; if (r && typeof (r as any)?.then === "function") (r as Promise<unknown>).catch(e => console.error("[exp-rs-spatial] onGeneration callback failed:", e));`，或在类型上把回调约束为返回 `void | Promise<void>` 并统一兜住；补一条"回调返回 rejected promise 时 start() 仍 resolve"的测试。

### P2-2 `exprs_status` 在 re-list 尚未完成时宣称 "none detected"——声称了一次它并未完成的比对
- 文件：`pi/exp-rs-spatial.ts:297-306`（配合回调 `:121-153`）
- 问题：`onGeneration` 里的 `tools/list` 是 fire-and-forget，`catalogDrift.added/removed/lastError` 在**之后**才写；而 `exprs_status` 只要 `bridge.generations > 1` 就直接渲染 `parts.length ? ... : "none detected"`。于是在"刚 respawn、re-list 还在飞"这段窗口里输出的是 `catalog drift since registration: none detected`——这是一次未发生的否证结论。更糟的是该 re-list 用默认 10 分钟请求超时，新子进程若卡住，这句假话可以挂 10 分钟；且 re-list 失败时 `lastError` 会覆盖，但成功路径会把上一次代的 `added/removed` 原样留着呈现为"当前"结论。
- 复现：`onGeneration` 后立刻调 `exprs_status`（沙箱内可用 `--hang-tool-calls` 的新子代复现：generation=2、re-list 永不回包 → 状态一直显示 "none detected"）。
- 建议：加 `relistInFlight` 标记，未完成时输出 `catalog drift since registration: re-list pending`，完成才允许 "none detected"；并在 re-list 成功分支里记录 `checkedAtGeneration`，把"无漂移"绑定到具体代际。

### P3-1 W2 投影只交付了 GOAL O3 五个字段中的四个，且 `guarantees` 用的是描述符默认值而非作者声明
- 文件：`src/agent/mcp_server.cpp:1597-1605`
- 问题：(a) GOAL O3 明确列出 `prerequisites/limitations/deterministic/idempotent/supportsCancellation`，实现漏了 `supportsCancellation`（`AgentMetadata::supportsCancellation` 默认 `false`，preflight `algorithm_preflight.cpp:646` 一直在暴露它，入口面却不暴露）；(b) `providerEntryMap():1634-1671` 完全不发 `guarantees`，于是 agent 无法区分"未声明"与"不适用"；(c) `deterministic` 默认 `true`（`algorithm_descriptor.h:85`）、`idempotent` 默认 `false`，把这两个**默认值**冠以 `guarantees` 键名，对从未声明的算子是被动断言（与 preflight 暴露同一字段的语义一致，所以不算虚构，但键名过强）。
- 建议：补 `supportsCancellation` 进 `guarantees`（或明确在 DECISIONS 里记录"仅 4 项，supportsCancellation 留给 preflight"以闭环 oracle 偏差）；考虑把键改名为 `planningHints` 或在条目里区分"声明"与"默认"。

### P3-2 run_id 诊断路径新增了未经 workspace 包含检查的绝对路径披露
- 文件：`src/agent/mcp_server.cpp:3207-3208`
- 问题：`steps[].output_path` 是绝对文件系统路径，直接出 MCP 边界；`artifact_read` 对同类信息有 `SICNU_MCP_WORKSPACE` 包含门（`surface_redaction.h` 也明确"路径不走文本 scrub，靠 sandbox 管"），而 pipeline 路径（`:3097-3115`）刻意不回路径。run id 由时间戳+随机后缀生成、不可猜测，且输出在 submit 时已过 workspace 校验，实际风险很低，但这确实是新开的披露面。
- 建议：要么对 `output_path` 过一遍 `containment::` 校验/相对化，要么在 `resume_hint`/描述里注明"路径来自本会话提交的 run，越权访问仍由 artifact_read 裁决"。

### P3-3 lazy-respawn 包装把不可重试的 `LAUNCH_FAILED` 改写成可重试的 `RESTART_FAILED`
- 文件：`pi/mcp_bridge.ts:572-584`
- 问题：二进制缺失（ENOENT/EACCES）时 `start()` 抛的是 `LAUNCH_FAILED`/`retryable=false`，但 `request()` 的 respawn 分支统一重打为 `RESTART_FAILED`/`retryable=true`，调用方按 `retryable` 自动重试会连续拿到错误的"可重试"信号，直到 5 次快崩溃把断路器锁死（自限，不会无限循环）。
- 建议：透传原错误的 `bridgeError`/`retryable`（`err.bridgeError ?? BRIDGE_ERRORS.RESTART_FAILED`），只在没有类型码时才兜底。

### P3-4 `pipelineIdForRun` 与 `runForRunIdReadOnly` 两次独立取锁之间存在 TOCTOU，`source` 可能被错标
- 文件：`src/agent/mcp_server.cpp:3138-3161`
- 问题：run 在两次调用之间 finalize（反向 map 摘除、`m_runsByPipeline` 仍保留）时，`pipelineId` 变 -1 而 run 仍从内存拿到 → `source` 报 `"checkpoint"`，实际来源是内存终态 run。纯诊断字段错标。
- 建议：改为一次查询同时返回 run 与 pipelineId（或在拿到 run 后再调一次 `pipelineIdForRun` 校正）。

### P3-5 死分支：`else if (state == "Canceled")` 永不可达
- 文件：`src/agent/mcp_server.cpp:3188-3192`
- 问题：`Canceled` 已在上面被判为 resumable，该分支不可达；注释自己也写了 "unreachable … kept for clarity"。属噪声而非缺陷，但会误导读者以为存在一条独立的 Canceled 语义。
- 建议：删除该分支，或改成 `else if (!resumable && isTerminal(state))` 之类的真实兜底。

### P3-6 `run_id` 字符守卫与它自己的错误文案不等价（PCRE `$` 会匹配结尾换行）
- 文件：`src/agent/mcp_server.cpp:3124-3125`（同形问题在 `handleResumeWorkflow:3251`，属预存）
- 问题：`^[A-Za-z0-9_.-]+$` 在 PCRE/QRegularExpression 语义下会接受 `"abc\n"`（`$` 匹配末尾换行前），而错误文案声称只允许 "letters, digits, '_', '-', '.'"。经 MCP 工具入口不可达（dispatch 处已 `.trimmed()`，`:1099`），仅直接 C++ 调用可达；且字符类已排除 `/`、`\` 并显式拒 `..`，无目录穿越后果。
- 建议：改用 `\\A[A-Za-z0-9_.-]+\\z`（或对 `runId` 先做一次显式的字符白名单循环），让守卫与文案一致。

### P3-7 E2E 崩溃链在不等旧宿主退出、不让出 flock 的情况下直接 resume，可能被状态墙拒绝
- 文件：`pi/test/r25_c_e2e_real_host.test.mjs:188-214`
- 问题：`bridge.stop()` 后立即建 `bridge2` 并 `resume_workflow(run_id)`；旧宿主退出前仍持有该 run 的 flock，`resumeRunImpl:1191-1206` 会以 "owned by a live process (pid N)" 拒绝 → `assert.ok(!resumed.isError)` 失败。TEST_RESULTS.md 记录该链"待二进制构建后执行"，即**从未实跑**，这是一条未验证的声明加一处合理 flake。
- 建议：kill 后轮询等待旧宿主退出（或轮询 `resume_workflow` 直到非 "owned by a live process" 类错误，带上界）；至少把"未执行"写进提交说明，别让 5 链 GREEN 的表述先于事实。

### P3-8 测试覆盖缺口（新增生产行为无对应断言）
- 位置：`src/agent/mcp_server.cpp:3084-3094`（pipeline 路径的 `resumable`/`resume_hint`）无任何测试；`LAUNCH_FAILED`、`ABORTED` 两个类型码无测试（`pi/test/r25_c_generation_drift.test.mjs` 只覆盖 8 个码里的 6 个）；`onGeneration` 抛错/返回拒绝 promise 的兜底无测试；`run_id` 优先于 `pipeline_id` 的优先级无测试。
- 建议：至少补 pipeline 路径 `resumable` 一条（该路径是更高频的入口）与 run_id 优先级一条。

### P3-9 死状态：`catalogDrift.serverVersion` / `previousServerVersion` 只写不读
- 文件：`pi/exp-rs-spatial.ts:53-60`、`:123-124`
- 问题：两个字段被赋值但无任何消费点；`exprs_status` 的版本信息实际来自 `bridge.serverInfo?.version`。同时 `:144` 的告警文案里 `info.previousServerInfo?.version !== undefined && info.previousServerInfo` 后半句冗余。
- 建议：删除死字段，或在告警里真正使用它们（当前告警只在 `added.length || removed.length` 为真时才打印，版本信息对"无漂移但换了二进制"的场景反而更有用）。

---

## 汇总

- P0：0
- P1：0
- P2：2（P2-1 异步回调拒绝未兜住；P2-2 "none detected" 声称了未完成的比对）
- P3：9

无阻塞级正确性/安全缺陷：并发与锁纪律、run_id 守卫与权限边界、resumable advisory 的诚实性、C++/JS 测试判别力（RED 实证）、轨道边界、snapshot 一致性均通过。两个 P2 都是小范围、本地化修复，且都落在本轨的核心主张（诚实/可诊断）上，建议合并前闭环；其余 P3 可作后续清理。

R1_VERDICT: NEEDS_FIXES（P0=0，P1=0，P2=2）
