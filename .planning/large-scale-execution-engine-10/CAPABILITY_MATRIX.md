# CAPABILITY_MATRIX — large-scale-execution-engine-10（Phase 0 基线盘点）

图例：✅ 基线已有 · 🟡 部分有（有缝） · ❌ 缺失（本 track 交付）

## A. Execution architecture baseline

| 能力 | 状态 | 备注 |
|---|---|---|
| TaskCenter 生命周期/取消/暂停/重试 | ✅ | 9 状态 + typed cancel + bounded auto-retry |
| JobEngine 池/优先级/独占 | ✅ | priority buckets + exclusive FIFO |
| ExecutionPlane 统一提交面 | ✅ | commit-once、event-loop 无关 await |
| WorkflowRunCoordinator | ✅ | 10 状态、锁外 drain、resume |
| worker host/pool 健康模型 | ✅ | 握手/超时/取消升级/回收/进程树围困 |
| output committer / artifact store | ✅ | 原子 temp→stable |
| execution cache / fingerprint | ✅ | V2 身份、LRU、持久池 |
| checkpoint（workflow 层） | ✅ | 原子写/版本门/恢复/ghost 隔离 |
| trace/fault registry | ✅ | observability 模块 |
| resource budget（任务级） | ✅ | budget1/2 多维 + RSS 水位 |
| 毒任务（poison task）识别 | ❌ | 同一 operator 反复崩 worker 无隔离升级 |
| 重复完成回调（duplicate completion） | 🟡 | terminal 去重存在；跨 worker 重投递语义未成文测试 |

## B. Tile DAG / Stream execution

| 能力 | 状态 | 备注 |
|---|---|---|
| TileSpec（tile/halo/bands） | ✅ | `src/runtime/chunk/tile_spec.h` |
| 线性 pipeline（producer→stages→consumer） | ✅ | ChunkPipeline + BoundedChunkQueue |
| 多输入 join / tile DAG（producer/consumer 图） | ❌ | 只有单链；无 fan-in |
| band subset / time chunk 维度 | ❌ | TileSpec 无 bandRange/timeIndex |
| deterministic partition | 🟡 | buildTileGrid 确定；但无"从 grid spec 派生分区"的声明式契约 |
| tile lifetime / reusable intermediate | ❌ | 中间 tile 只能驻内存（队列容量内） |
| 能力契约：streamable/full-raster/two-pass | ✅ | RSOperatorMemoryPolicy |
| 能力契约：neighborhood/halo 半径声明 | ❌ | 无 halo 半径/窗口依赖声明 |
| 能力契约：global-reduction 声明 | ❌ | 无"需要全局两遍"的显式等级 |
| 能力契约：external-memory 声明 | ❌ | 无 spill-to-disk 等级 |

## C. Memory planner

| 能力 | 状态 | 备注 |
|---|---|---|
| 任务级 RAM 估算（registry/preflight/override） | ✅ | resolveEstimateMb + estimateExecution |
| 多维任务准入（RAM/VRAM/tempDisk/IO 权重） | ✅ | budget1/2 + TaskCenter gates（默认多数 off） |
| tile 级 working-set 规划器 | ❌ | 无 (input window+output+halo+stages+queueCap) 规划 |
| 并发自动降档（reduce concurrency） | 🟡 | 准入 gate 只延迟启动；不降 tile 并发 |
| spill 决策 | ❌ | 无 |
| actionable refusal（可执行的拒绝估算） | 🟡 | WaitingResource reason 有字符串；无"需要 X MiB，可用 Y"结构化估算 |
| no bad_alloc as control flow | 🟡 | readWindow 有 maxBytes 预算；chunk 层无 planner 兜底 |

## D. Backpressure

| 能力 | 状态 | 备注 |
|---|---|---|
| 有界队列（chunk 内） | ✅ | BoundedChunkQueue |
| 生产者节流 | ✅ | 队列满阻塞 producer |
| disk writer 节流 | ❌ | 无有界写队列 |
| remote I/O 节流 | 🟡 | budget2 networkWeight 维度存在；默认 off、无范围缓存级联动 |
| GPU admission | ✅ | ModelSessionPool VRAM 预算 + OOM ladder |
| worker 饱和保护 | ✅ | kMaxTransientWorkers / pool 上限 |
| 取消传播 | ✅ | DAG + ownership 双边（ep9） |
| 无死锁论证 | 🟡 | 线性链有测试；多输入 join 无 |

## E. External-memory primitives

| 能力 | 状态 | 备注 |
|---|---|---|
| tiled scan | ✅ | GdalBlockStream（单带单线程）+ ChunkPipeline |
| multi-pass reduction helper | ❌ | 各算子手写两遍循环 |
| disk-backed intermediate（tile store） | ❌ | 无 |
| external sort/merge | ❌ | 无（按需） |
| chunked table | ❌ | 无 |
| scratch 预算/登记/清理 | 🟡 | 取消时按路径嗅探 `.scratch` 清理；无登记处、无预算 |
| temp naming | ✅ | QTemporaryImage/output committer tmp 惯例（散布） |
| atomic finalize | ✅ | OutputCommitter / checkpoint 原子 rename |

## F. Content-addressed execution cache

| 能力 | 状态 | 备注 |
|---|---|---|
| input identities / params / implementation hash | ✅ | V2 fingerprint |
| chained producer identity / remote ETag | ✅ | #726 / 8.0 WP-F |
| deterministic grade 门 | ✅ | 构造 operator 时 schema+grade hash |
| output digest / 持久池 | ✅ | ArtifactObjectPool + digest 复验 |
| stale validation / 自愈 | ✅ | stat 校验失败即擦除 |
| environment relevant pins | ❌ | 无 env 维度（GDAL 版本、SICNU_* 行为开关） |
| cache refusal for stochastic ops | ✅ | 非 bit_exact 不入 fingerprint（保守） |
| partial corruption recovery | ✅ | lookup 时 digest/stat 复验 |

## G. Checkpoint / Resume

| 能力 | 状态 | 备注 |
|---|---|---|
| workflow step 复用/恢复 | ✅ | ADR 0123/0144 |
| checkpoint version / 原子写 / crash restart | ✅ | WorkflowCheckpointManager |
| stale checkpoint（实现身份戳） | ✅ | 8.0 WP-E |
| 参数/输入漂移 | ✅ | fingerprint 复验 |
| user cancel vs crash 区分 | ✅ | #684 shutdown ≠ user；run lock 所有权（#727） |
| **long-task（tile 级）in-progress resume** | ❌ | 长任务中途崩溃只能整任务重来 |

## H. Resource scheduler

| 能力 | 状态 | 备注 |
|---|---|---|
| CPU slots / RAM / VRAM / IO 权重统一准入 | 🟡 | budget2 全维度在 TaskCenter 准入，但大多数 gate 默认 off 且 descriptor 很少声明 |
| GPU/VRAM 与 Model Runtime device truth 对接 | 🟡 | 模型会话池用 NVML 真相；TaskCenter 的 vram 维度不吃 NVML |
| external process 槽 | ✅ | profile limits（CLI/Python worker） |
| exclusive resource | ✅ | JobEngine exclusive jobs |
| remote provider quota | 🟡 | networkWeight 维度；无按 host 的配额 |
| scratch disk 预算准入 | 🟡 | tempDisk 维度存在；无 tile 级 scratch 记账 |

## I. Worker model

| 能力 | 状态 | 备注 |
|---|---|---|
| heartbeat / health / crash / respawn / 回收 / 孤儿清理 | ✅ | pool + guard |
| idempotent dispatch | ✅ | submitWithId #799 + terminal 去重 |
| poison task 隔离 | ❌ | 反复崩同一 operator 无升级路径 |
| duplicate completion handling | 🟡 | 有去重实现；缺 storm 级测试 |
| process group cleanup | ✅ | WorkerProcessGuard |
| remote-worker protocol seam | 🟡 | v1 线协议 forward-compatible；无 remote 端点（按 goal 保持 seam 即可） |

## J. Scale tests（基线已有：test_execution_plane_9 / 7 / concurrency_stress / chunk_graph）

| 场景 | 状态 |
|---|---|
| 100k 逻辑任务 DAG | ✅ |
| 300 步深链 | ✅ |
| cancel storm（60 任务） | ✅ |
| worker crash storm | ❌ |
| bounded scratch 压测 | ❌ |
| cache hit/miss 规模 | ❌ |
| checkpoint restart 规模 | 🟡（有 fault-injection 单测，无规模） |
| 10^6 逻辑 tile 流 | ❌ |
| 资源饥饿/老化 | 🟡（aging 单测，无 storm） |

## 缺口 → 工作包映射（本 track 交付）

| 工作包 | 关闭的缺口 |
|---|---|
| WP-A 能力契约 | B 的三个缺失声明维度 + schema/metadata/help 投影 |
| WP-B Tile DAG / Stream Graph | B 的 join/band/time/lifetime/partition |
| WP-C Memory Planner | C 全部 |
| WP-D Backpressure/Writer throttle | D 的 writer/remote 节流 + 无死锁论证 |
| WP-E 外存 primitives + scratch 登记 | E 全部 + H 的 scratch 记账 |
| WP-F cache env pins | F 的 environment pin |
| WP-G 长任务 tile checkpoint | G 的 in-progress resume |
| WP-H 调度默认值 + NVML 对接 + 毒任务 | H 的 gate 默认与 device truth、I 的 poison |
| WP-J Scale tests | J 全部缺失行 |
