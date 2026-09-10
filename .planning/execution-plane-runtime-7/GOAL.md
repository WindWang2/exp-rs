# GOAL — Execution Plane / Worker Runtime / Cache / Recovery 7.0

Branch: `feat/execution-plane-runtime-7` (worktree `../exp-rs-execution-plane-runtime-7`, cut from master `2041f6fa`).

把 TaskCenter → JobEngine → WorkflowRunCoordinator → worker/runtime/cache/recovery 推进为
可承载长时间、大型遥感/模型工作流的生产级执行平面。

## 唯一调度链（不变量）

`WorkflowRunCoordinator -> TaskCenter -> JobEngine -> Executor`

worker process 只能作为 Executor 实现；不允许出现第二个 job graph/scheduler。

## 开发包

| 包 | 内容 | 里程碑 |
|---|---|---|
| A | LocalWorkerPool 生产接线（executor selection、isolated-process、reuse、handshake、health、generation、max jobs/lifetime、recycle、crash replacement、graceful shutdown、Win/Linux parity、worker crash 不带崩 GUI） | M2 |
| B | Resource-aware admission（CPU/RAM/GPU-VRAM/temp-disk/ext-proc/IO-hint/manifest hints，有界防 OOM） | M3 |
| C | Hierarchical cancellation（workflow→task→executor→worker→operator→tile/batch） | M4 |
| D | Crash/Resume 3.0（content identity、completed-step verification、partial artifacts、worker/app crash、changed inputs/operator、moved output、有界 retry classes） | M5 |
| E | Output Committer / Provenance（atomic publish、artifact identity、derivation、governed registration、verification status） | M6 |
| F | Execution Cache 3.0（stable fingerprint、content identity、chained reuse、external mutation、remote identity seam、cross-run、bounded disk GC、hit 仍验证） | M7 |
| G | Worker Protocol Robustness（capabilities/health/telemetry/structured errors/progress/cancel-ack/output identity/crash diagnostics；升级必须带协商+向后兼容） | M1 |
| H | Concurrency/Deadlock 审计（child work、sync waits、Qt affinity、callback order、completion-before-registration、shutdown races、listener lifetime、pipes、bounded queues） | M8 |

## 完成定义

大型 RS/AI 工作流即使出现 worker crash、OOM、取消、应用重启，也应得到可解释、
可恢复、不会伪造成功的行为。

## 约束

- 最多 2 个 subagents（#1 已用于基线审计，#2 保留给最终 adversarial review）。
- 无线上 CI；一切结论来自本地可重复构建/测试。
- `CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`；重目标 -j1/-j2。
- master 只读；只在 worktree 开发；small commits；先 contract 后功能。
- FAIL 不得包装成 success；无法验证用 warning/unknown。
