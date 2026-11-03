# r27-T2: 数据资产、遥感 I/O 与分块/模型执行运行时 — 可靠性与可恢复性

**base** `10652fd003`（=origin/master@2026-11-02 侦察时刻）→ **head** 见 PR（分支 `agent/r27-t02-data-io-runtime-reliability-20261102T132440Z`，worktree 独占）。

## 首要：修复 master 的 tests/CMakeLists.txt 交错破坏（阻塞 configure）

master 自 #1727 起 5 次 "Merge origin/master into <track>" 的 append-only 冲突解决把各轨追加块交错打断在语句中间（CMake parse error at :17426，全仓无法 configure）。重建以 #1726（最后平衡点）为基座、按 PR 合并序重放 r25-C/r25-A/r26-harness/r26-workbench/r26-science 五个 authored 块：括号/字符串平衡 OK；**1102 个测试注册目标与 master 完全一致**；行多重集为 master 严格超集（多出的 36 行是交错合并吞掉的 authored 内容，全部找回）。同类先例 #1715。

## Issues（全部 OPEN at base，代码证据见 `.planning/r27-t02/BASELINE.md`）

| Issue | 修复 |
|---|---|
| **#1743 (P1)** | `ChunkGraph::addJoin({A,A})`：同调用内重复输入在**任何变更前**以 typed `logic_error` 拒绝（强异常保证：被拒 join 不消费输入）；空 join/越界 id 从 assert 升为运行时拒绝（RIO-3）。8 用例反例矩阵（重复/间隔重复/强保证复用/合法 A+B/显式拷贝扇出/协作取消/跨调用守卫不弱化）。**RED 已证明**：master 代码 4/4 失败 → 修复 8/8 通过 |
| **#1764 (P2)** | worker 租约表有界：`teardownWorker`/`retireWorker`/两个 spawn 失败路径全部 `reset()`；`trackedCount()` 可观测 + `health().trackedLeases`；500 代 counter-example 与真实 pool（4 代 recycle）断言 |
| **#1765 (P2)** | 新后端权威 `geo::remote::safeRemoteRasterPath`：scheme→offline→私网 deny-by-default（环回/RFC1918/链路本地/云 metadata，DNS-free）→range-cache 必须安装（否则 typed 拒绝，绝不降级 raw /vsicurl/）。合法 https 映射到受控 transport 路径。**GUI 侧（stac_browser_dialog）按目标文件条款专归 T4**，本轨不动 src/app/** |
| **#1766 (P2)** | `ResumableTileRun` 状态路径单拥有者：Qt-free OS 锁（flock/LockFileEx）+ typed `ChunkRunLockHeld` fail-closed；崩溃由内核释放（真实子进程 _exit 持锁后确定性恢复：5 tiles 复用）；锁文件永不 unlink（双 inode 竞争） |
| **#1767 (P2)** | 有界 shutdown：worker detach + immortal 单例 + `m_liveWorkers` 计数 deadline 等待（默认 5s，可注入），超时**放弃**卡住 worker（返回 stuck 数）；可选 per-job `timeoutMs` watchdog（fire cancel path）；RIO-9 progress 终态门；RIO-10 晚到取消报 Succeeded（结果保留 + 日志注明） |
| **#1803 (P3)** | RIO-6 acquire 走受控路径；RIO-7 WHERE 语句断 token typed 拒绝；RIO-11 gov_audit 双写表退役 + audit 100k 有界保留；RIO-12 `OSRSetFromUserInputEx` + ALLOW_NETWORK/FILE_ACCESS=NO；RIO-13 manifestVersion/chunkKey 双信号（仅删 index 无法降级 v1）；RIO-14 重复 continuation 立即停止 + guard 10 万→1 万 |

## 里程碑 D/E

- **D**：`ImportSession` 四状态（检查→继续导入→取消→恢复），复用 ImportPreflight（R25-B）与 DataManager dedup——无第二身份系统；journal 经 platform::sidecar 原子发布，机器可读摘要 schema `sicnu.import.session/1`（含 journal_write_error 诚实性字段）。
- **E**：尾批（100×60@32×32）像元守恒 + 字节级一致；注入 journal 失败恰 1 块重算；取消恢复零重算。

## 验证（全部本地，未等远程 CI）

- 本轨 39 用例**双跑 100%**（R2 闭环含 4 个新增 oracle：RIO-14 判定、v1 manifest 兼容、audit retention、CRS 拒绝消息）；RED 证明 #1743（master 4/4 fail → fix 8/8 pass）。
- 回归：test_chunk_graph/resume_11/worker_lease_11/remote_pool_init/offline_mode/mirror_maintenance/governance_store/vector_contract（23/23 ×2）、**test_job_engine 34 用例 ×2**、test_worker_host 13、**test_execution_plane_7（10050 断言）**、test_execution_scale_fault_11、test_chunked_operators_parity、test_r14_B_tile_engine（20695 断言）、test_r15_C_chunk_admission —— 全绿。
- 评审：R1（并发/生命周期，独立 agent）**11 findings 全修**；R2（地理空间契约/安全 I/O，独立 agent）**13 findings 全修**——含 GDAL 3.8 lane 守卫、safe-open 受控姿态门（GuardUnavailable）、r12b 回归修复、v1 兼容与 retention oracle（见 `.planning/r27-t02/REVIEW_*.md`）。
- RIO-12 残余范围如实声明：`geo::osrSetFromUserInputNoNetworkNoFile` 已公开并接线白名单内 2 处 untrusted 站点；其余 ~13 处 SetFromUserInput 站点分布在 agent/core/analysis/operators-rs/processing（跨轨白名单外），留待各归属轨采用同一 helper。

## 写入边界

白名单 glob + 里程碑 B 点名的两处（`src/jobs/**`、`src/processing/framework/local_worker_pool.*`，决策 D-1 记录于 `.planning/r27-t02/DECISIONS.md`）。tests/CMakeLists.txt 仅 reconstruction commit 有删除（修复 master 自身破坏），其后全部 append-only。未触碰 src/app/**、src/operators/rs/**、src/agent/**、src/python/**。

## 已知限制 / NOT_EXECUTED

- Windows 分支（LockFileEx/Job Object）仅编译目标存在，未在 Windows 运行。
- 大栅格 256/512/1024 吞吐矩阵、GPU→CPU fallback 资源峰值：未做（非本轨 Oracle；见 BENCH_BASELINE.md 声明）。
