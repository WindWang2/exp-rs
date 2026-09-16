# CAPABILITY_MATRIX — 执行底座能力 before/after（Execution Runtime 11.0）

图例：✅ implemented（本 track 后，有本地测试证据）｜🟡 partial（有约束/降级）｜❌ not-supported（诚实声明）｜⬜ before 状态（master@a5b11b7f）。

| 能力 | before (10.0) | after (11.0) | 证据（测试/命令） |
|---|---|---|---|
| 单一 task authority（无第二 scheduler） | 事实存在但无 gate | ✅ 源扫描架构网冻结 36 文件 allowlist + 行为测试（worker 线程 1:1 job id） | test_execution_authority_11 |
| ChunkPipeline consumer-abort fail-closed | ❌ 静默正常返回（fail-open） | ✅ `ChunkConsumerAborted`（ChunkCancelled 子类）抛出 | test_execution_authority_11 / test_chunk_graph |
| fused_chain 取消传播 | ❌ cancelFlag 死代码 | ✅ ChunkCancelBridge 桥接（flag+callback）+ 类型化翻译 | 代码路径 + test_chunk_contract_11（bridge 合约） |
| 统一 TileRun 合约（identity/halo/determinism） | ❌ 无（每算子手写） | ✅ tile_run_contract.h：partition digest 稳定+漂移敏感（独立 FNV oracle） | test_chunk_contract_11 |
| 错误信封 chunk→RSOperatorError | ❌ 两套体系无桥 | ✅ 全表映射（Cancelled/CorruptArtifactData(2005)/ResourceBudgetExceeded(4103)/ComputationError） | test_chunk_contract_11 |
| crash-safe resume（库级原语） | 🟡 TileCheckpoint/ScratchRegistry/DiskTileStore 存在但零生产接线 | ✅ ResumableTileRun：journal+checkpoint+identity 门+损坏自愈+stale wipe | test_chunk_resume_11 |
| 真实进程崩溃恢复 | ❌ 无验证 | ✅ 子进程 _Exit(70) 硬杀后：7 tiles 零重算、输出 byte-equal、恰好一次出版 | test_chunk_resume_11（[crash]） |
| exactly-once publication（run 级） | ❌ 无 | ✅ PUBLISHED marker（tmp+rename）；重复执行零 kernel 调用 | test_chunk_resume_11 / test_chunk_adoption_11 |
| 身份漂移拒绝复用 | 🟡 checkpoint 有门但从未在运行路径使用 | ✅ operator/input/partition 三重门；漂移→wipe+fresh，绝不混用 | test_chunk_resume_11（R1） |
| journal 损坏语义 | ❌ 无 journal | ✅ 撕裂尾截断恢复；中段损坏 typed 失败（绝不静默） | test_chunk_resume_11（R3） |
| 内存模型可作硬准入门 | ❌ F-A-13 漏算 in-hand tile（低估） | ✅ 上界模型（(S+1)·cap·I + (S+2)·I + 1 tiles）≥ 随机调度模拟峰值 | test_execution_governor_11（模拟器 oracle） |
| 统一资源治理面 | 🟡 planner advisory + 零接线的 gate/registry | ✅ ExecutionGovernor：RAM/scratch/write-gate 一体 + 降级阶梯 + typed 拒绝 | test_execution_governor_11 |
| 资源泄漏检测 | ❌ 无 | ✅ 析构期 outstanding → DiagnosticReport(exp.diag.v1) + resource_leaks_detected 计数 | test_execution_governor_11 |
| worker lease 过期判定 | ❌ 仅 heartbeat 帧定义、宿主不追踪 | ✅ WorkerLeaseTracker（注入时钟）：job-held 静默 TTL→Expired（episode 计数） | test_worker_lease_11 |
| worker poison 隔离 | ❌ 连续 operator 失败仍复用 worker | ✅ 连续失败阈值→Quarantined（sticky），下次 idle 扫描回收；成功清零 streak；reset=重启恢复 | test_worker_lease_11 |
| 有界接管重试 | ❌ 无策略 | ✅ mayTakeover 硬界（attempt ≤ maxTakeoverRetries）；池保持 fail-loud 不自动重试 | test_worker_lease_11 |
| per-chunk 遥测 | ❌ 词汇已定义零发射 | ✅ QueueWait/ChunkProgress/ExecutionEnd 采样发射 + tiles_processed 精确计数；事件 ≤ tiles/rate+O(1) | test_execution_telemetry_11 |
| 遥测内存上限 | 🟡 ring 8192 存在但无发射者 | ✅ 洪泛测试 ≤ kEventCapacity；采样界断言 | test_execution_telemetry_11 |
| DiagnosticReport 生产发射 | ❌ 零生产消费者 | ✅ governor 泄漏路径为首个生产发射者 | test_execution_governor_11 |
| adoption kit | ❌ 无（~24 处手写循环） | ✅ runChunkedOperator（Resumable/Pipeline 双模式，byte-equal 合约） | test_chunk_adoption_11 |
| 10^6 逻辑 tile 规模 | 🟡 chunk_plan 有、resume 无验证 | ✅ O(1) 网格算术 + digest 稳定；opt-in 100k 真实 journal 往返（SICNU_SCALE_11=1） | test_execution_scale_fault_11 |
| 取消风暴收敛 | ❌ 无验证 | ✅ 8 轮种子化取消→完成：输出 byte-equal、零多余重算 | test_execution_scale_fault_11 |
| 生产域算子全面迁移 | — | ❌ 有意不做：本 track 只交付 synthetic reference adopter（避免与 20 个域 track 冲突）；迁移按 ADOPTION_GUIDE 由各域 track 进行 | — |
| Windows fsync 持久性 | 🟡 no-op（rename 为原子边界） | 🟡 同前（D-016：沿用既有契约，不改 fsync 语义） | DECISIONS D-016 |
