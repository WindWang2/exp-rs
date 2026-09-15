# DECISIONS — 执行裁决记录（autonomy=full，不向用户提问）

| ID | 决定 | 理由（含被否候选） |
|---|---|---|
| D-001 | 以启动审计为准 rescope：#991/#992 已合并，条件 read-only（src/workflow、src/dataset、src/app/workbench）解除；但仍不写这些目录（非本 track 业务主体）。 | GOAL 明示"以启动时事实为准"。 |
| D-002 | Open issues #1001–#1007 不修，登记 OUT_OF_SCOPE。 | 均在 io/workflow/dataset/agent/georef 域，跨 primary scope；R2 review 残留由各域 track 处理；修了反而制造并发冲突。 |
| D-003 | 构建入口用 configure preset `dev-default`（binaryDir `build-dev`）。 | GOAL 说 "CMakePresets.json/build-dev"；preset 列表中无名为 build-dev 的 preset，dev-default 的 binaryDir 即 `${sourceDir}/build-dev`，语义吻合。 |
| D-004 | PARALLEL_OWNERSHIP 等 artifacts 写入本 track worktree 的 `.planning/execution-runtime-convergence-11/`（而非主仓库工作树）。 | master 只读；主仓库工作树有用户未提交改动，不可污染。审计数据先收集于会话，落盘于 worktree，与 runbook 第 1 步一致。 |
| D-005 | ChunkPipeline consumer-abort 语义从"静默正常返回"改为记录错误并重抛（新异常 `ChunkConsumerAborted`）。 | 现状 fail-open（写失败被当成功）；ChunkGraph 已采用"abort=可取消终态"先例。候选"维持现状+文档"被否：违反 fail-closed 原则与 Oracle 3。行为变化同步更新既有测试预期。 |
| D-006 | fused_chain.cpp 做 ~30 行最小集成修复（cancel 桥 + 异常翻译），尽管在 src/processing/framework。 | 它是 ChunkPipeline 唯一生产消费者；不修则"取消传播进 chunk 执行"不可证明（Oracle 1/3）。改动为接线性质，不新增功能面。 |
| D-007 | 统一 chunk 合约放 `src/runtime/chunk/`（Qt-free、operator-independent），RSOperatorError 翻译层放 `src/operators/framework/`。 | 依赖方向 operators→runtime 已存在；反向会把 Qt/算子语义漏进 Qt-free runtime（被否）。 |
| D-008 | resume journal 采用 append-only 行式文本（tileIndex、digest、bytes），checkpoint 保持既有 TileCheckpoint v1 格式不动（新字段通过 RunJournal 承载）。 | 格式版本兼容（kTileCheckpointFormatVersion=1 不变，旧读写方不受影响）；候选"扩展 checkpoint 二进制格式"被否：破坏 v1 兼容且 O(N) 重写。 |
| D-009 | exactly-once publication 用 run 级 `PUBLISHED` 标记文件（原子 tmp+rename），而非引入 OutputCommitter 依赖。 | runtime 层 Qt-free 且不应依赖 processing 层；OutputCommitter 是任务级出版权威（不变），tile-run 级 marker 是其下层的幂等基元。 |
| D-010 | worker lease/poison 以 host 侧 `WorkerLeaseTracker`（注入时钟）实现，协议不加新 op（heartbeat/progress 即 liveness 信号）。 | protocol v1 extension rule：新 op 需所有旧 worker 容忍，但 host 侧追踪零协议风险即可满足单机故障隔离；候选"新增 lease op"被否：多余且破坏兼容承诺。 |
| D-011 | 遥测采样默认 N=16（每 16 tile 一个 span 事件 + 首/末必发），计数器每 tile 原子自增（零内存成本）。 | 8192 ring 上限下 1e6 tile 逐 tile 发射会自我驱逐 99% 事件；采样保持统计代表性且事件量有界（≤ tiles/N + O(1)）。 |
| D-012 | memory_planner 修复方向：把 per-stage in-hand tile 计入模型使估计成为保守上界（宁可高估拒算，不可低估放行）；joinInputCount 保守应用于全部 stage。 | 作为硬 gate 的方向必须是 fail-closed（高估→Refuse 是安全侧）。候选"精确下界模型"被否：需要运行时配合，复杂且仍非严格界。 |
| D-013 | Reference adopter 只在测试中注册（synthetic），不加入 rs_operators_init builtin 清单。 | GOAL WP-G 明示本 track 只做 synthetic/reference adopter；生产域算子改造属各域 track follow-up。 |
| D-014 | 新测试命名 `test_{chunk,execution}_*_11.cpp`，落在 GOAL primary scope 的 tests/*execution*、tests/*chunk* 模式内。 | 命名规则沿用仓库 `<域>_<主题>_<track#>` 先例，冲突最小。 |
| D-015 | 子进程崩溃恢复测试用测试二进制自重执行（env 开关 + `_Exit`），不用 fork（Windows 无 fork）。 | hermetic、跨平台；tests/helper_external_process.cpp 先例存在。 |
| D-016 | Windows 下 fsync no-op 是既有事实；resume 原子性边界声明为 rename。不试图引入 Windows FlushFileBuffers 大改。 | 与 scratch_registry/tile_checkpoint 既有契约一致（"rename is the atomicity boundary"）；改 fsync 行为跨平台风险大且超出本 track 可验证范围。已知限制记 PR_BODY。 |
