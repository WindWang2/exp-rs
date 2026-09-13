# REVIEW_LOG — large-scale-execution-engine-10

Phase 7 对抗 review：Subagent A（架构/并发/正确性，verdict NOT-MERGEABLE：4 P1）+ Subagent B（测试可信度/边界/性能，verdict MERGEABLE-WITH-FIXES：4 P2）。主代理逐条在 HEAD 代码复核后处置如下。

| # | 级别 | Finding | Disposition | Evidence |
|---|---|---|---|---|
| F-A-1 | P1 | ChunkGraph join 在取消/abort 竞态下误报 ChunkPartitionMismatch | FIXED：抛 mismatch 前复查 isCancelling()，unwind 中并入取消路径 | 修复 commit + join+abort 竞态回归测试 |
| F-A-2 | P1 | ScratchRegistry Entry::registry 裸指针析构 detach 竞态 (UAF) | FIXED：改 std::atomic<ScratchRegistry*> | 修复 commit |
| F-A-3 | P1 | preflight 描述符 tileWidth/Height=0 除零 SIGFPE | FIXED：clamp ≥1（同 F-B-2） | 修复 commit + tilePlan 测试 |
| F-A-4 | P1 | VramWireResult.unavailableReason 返回悬垂 const char* | FIXED：改 std::string | 修复 commit |
| F-A-5/F-B-1 | P2 | DiskTileStore 读先按未验证 payloadBytes 分配（OOM/bad_alloc 而非 typed 错误） | FIXED：先验 header（magic/version/headerDigest），再对 file_size 校验 payloadBytes，最后分配 | 修复 commit + 超大 payloadBytes 拒绝测试 |
| F-A-6 | P2 | finalize CAS 后 rename 失败被吞、永久 finalized | FIXED：rename 失败回滚 finalized 并抛 typed 错误；并发败者等待胜者完成 | 修复 commit |
| F-A-7 | P2 | ChunkGraph source 载荷无 buffer/spec 校验（与 ChunkPipeline 不等价） | FIXED：source 输出加 validateBuffer | 修复 commit |
| F-A-8 | P2 | consumer-abort 语义与 pipeline 分裂 + 异常无公共基类 | FIXED：ChunkGraphCancelled 继承 ChunkCancelled（pipeline 的 catch 继续有效）；abort 语义差异文档化（graph 需要可区分的取消终态） | 修复 commit |
| F-A-9 | P2 | progress 分母移动导致倒退/不到 1.0 | FIXED：单调钳制（只报递增值）+ 文档 | 修复 commit |
| F-A-10 | P2 | fan-out 静默数据分裂 | FIXED：建图时消费者计数 >1 抛 std::logic_error | 修复 commit + 测试 |
| F-A-11/F-B-12 | P2/P3 | env-pin 安装时序依赖 + provider 无锁 | FIXED：provider 存取加 mutex；提取 installExecutionEnvironmentPins()（once_flag）由 TaskCenter 与 WorkflowRunCoordinator 双入口调用；测试 RAII guard | 修复 commit |
| F-A-12 | P3 | ADR 相对实现过度声明 | FIXED：ADR 措辞收敛（scratch-backed payload 引用、poison escalation 标注为 staged/以既有 bounded retry + 测试钉死） | ADR diff |
| F-A-13 | P3 | planner 模型非严格上界 | FIXED：头注释明确 heuristic 及严格化前提（硬门控前须含 in-hand 项） | header diff |
| F-A-14 | P3 | scratch 预算算术不饱和 | FIXED：saturatingAdd 口径 | 修复 commit |
| F-A-15 | P3 | checkpoint tmp 名仅 pid 唯一 | FIXED：pid+进程级原子计数 | 修复 commit |
| F-A-16 | P3 | scratch acquire/release 持锁做文件 IO | ACCEPTED：当前无生产并发压力；拒绝热路径在 IO 前抛出；注释记录 | 代码注释 |
| F-A-17 | P3 | sweepStale 依赖目录 mtime | ACCEPTED：限定 startup-only 单实例用途 + 文档化 | header 注释 |
| F-A-18 | P3 | ChunkGraph 误用防线全 assert | PARTIAL-FIXED：double-run 升级为 throw；其余保留 assert + 头注释前置条件 | 修复 commit |
| F-A-19/F-B-10 | P3 | preflight resources 两个分支形状漂移 | FIXED：无条件 result["resources"]["tilePlan"] | 修复 commit |
| F-B-3 | P2 | preflight tilePlan 零测试覆盖 | FIXED：新增 stub 算子 + GDAL 微型 fixture 的 tilePlan 契约测试（含 tileWidth=0 拒绝） | test_preflight_tileplan（并入 test_atomic_algorithm_adapter） |
| F-B-4 | P2 | NVML 桥死代码 | FIXED：wireVramBudgetFromDeviceTruth 增加可注入 inventory 参数 + 数学测试（reservePercent 0/100、unknown total）；激活定位为宿主 opt-in seam（D-6 默认 gate off 不变） | 修复 commit + 测试 |
| F-B-5 | P3 | multi_pass_reduction 无测试 | FIXED：reduceTiles/reduceStream + 取消部分状态单测 | test_chunk_graph 追加 |
| F-B-6 | P3 | 损坏测试打在 header 区而非 payload 区 | FIXED：payload 区(offset 100)与 header 区两个用例 | test_external_memory_10 diff |
| F-B-7 | P3 | storm observedPeak 死仪表 + temp 残留 | FIXED：REQUIRE(observedPeak ≤ budget) + tempRoot 清理 | test diff |
| F-B-8 | P3 | retry/maxWorkers 未恢复 | FIXED：guard 保存/恢复 | test diff |
| F-B-9 | P3 | NMS 测试未断言 ErrorCode::Cancelled；dedup 取消未测 | FIXED：两处补齐 | test_model_tasks diff |
| F-B-11 | P3 | 未知 dtype 退化为 1 byte/sample | FIXED：未知 → 4（保守） | 修复 commit |
| F-B-13 | P3 | cache 测试在 SICNU_ARTIFACT_CACHE=1 时污染持久池 | FIXED：测试内 unsetenv + 指向临时目录 | test diff |
| F-B-14 | P3 | BoundedWriteGate 惊群/oversized 饿死风险 | ACCEPTED：当前规模可接受；头注释记录已知局限 | header 注释 |
| — | — | 透镜 8（历史修复回归）：无发现；Windows 可移植性：无发现；10^6 压测断言质量：无发现（B 正面确认界=19 推导） | VERIFIED | A/B 报告 |

清零结论：P0=0；P1：4 fixed / 0 accepted；P2：全部 fixed 或带测试；P3：fixed 为主，3 条 accepted 有理由。
