# R9 Track: Agent Harness / 执行框架 / 运行时 / 插件 SDK 稳定性收敛（方向 B）

## 目标

围绕现有 MCP/Pi 接入、Agent 工具调度、Workflow/TaskCenter、插件 SDK、隔离 Python worker、
模型推理与运行时，消除生命周期竞态、取消/恢复/幂等、错误语义和内存泄漏问题。
保持现有 Pi 边界，不重建 Agent 框架，不启动新产品方向。

## Oracle（可客观验证）

1. 每个采纳问题具备 Bug → Root Cause → Repro(red) → Fix → Regression(green) 证据链。
2. 本 worktree 定向 target 编译通过；受影响 CTest 套件绿灯（关键 gate 至少执行两次）。
3. 两轮独立 review（reviewer 未参与对应补丁实现），P0/P1 findings 清零。
4. 最新 origin/master 合并进本分支后回归仍绿。
5. PR 创建（base=master），描述含 base/head SHA、Issue/PR/review 对照矩阵、证据、遗留风险。
   不自动合并、不等线上 CI。

## 硬性边界

- master 只读；只在 `hardening/r9-agent-execution-reliability` 分支 + 独立 worktree 修改。
- 编译 -j2 上限（CMAKE_BUILD_PARALLEL_LEVEL=2）；CTest -j1。
- subagent 总派生名额 ≤ 3（本波已用 2 个审计）。
- 不触碰 A 方向（analysis/geospatial/data/dataset/experiment kernel）与 C 方向（UI/teaching/help/tests infra）文件。

## Work Packages（第一波，均经审计确认 REMAINING）

| WP | 问题 | 文件 | 来源 |
|----|------|------|------|
| A | setEventSink 回放在 mMutex 下回调，sink 再入 protocolFailure()/close() 同步死锁 | src/sdk/exprs/ipc_channel.cpp:412-431 | PR#1469 thread PRRT_...WRx |
| B | Windows 前斜杠 rooted argv[0]（/tools/worker.exe）probe 与 spawn 判定不一致；PATHEXT 被忽略 | src/sdk/exprs/external_process.cpp | PR#1469 thread PRRT_...WR2 + #1460-2 |
| C | record() 每次查询 push_back 新 PluginRecord snapshot，永不回收（内存无界） | src/sdk/exprs/plugin_registry.cpp:259-277 | PR#1469 thread PRRT_...WRu |
| D | 同 key GPU model 并发 load：两 ticket 重复请求 + VRAM admission 竞争 → 假拒绝 | src/operators/runtime/model_runtime.cpp:921-986 | PR#1469 thread PRRT_...WRq |
| E | m_catalog 裸指针锁外解引用，宿主不经 shutdown() 销毁 DataManager 时 UAF | src/processing/framework/task_center.cpp:1614,4086 | #1454-5 |
| F | acquireWorker processEvents 忙等 + adapter 嵌套轮询 → 重入双占窗口 | src/python/isolated/python_worker_process_pool.cpp:134-152 | #1462-7 |

已确认 ALREADY_FIXED_IN_MASTER（不重做）：B1a mcp_server recording handshake（commit 3305166e7）；
#1447 全部 B 侧站点；#1448 B 侧；#1449 B 侧；#1454 P1 + committer + logBuffer + worker pool 线程亲和；
#1455 全部；#1459 全部；#1460 P1 + ipc replay 顺序 + batch runner；#1462 P1 + rebind + unknown-method + dup 注册 + sidecar；#1463 全部。详见 PR_ISSUE_REVIEW_MATRIX.md。
