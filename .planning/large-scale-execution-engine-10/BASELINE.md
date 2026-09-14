# BASELINE — large-scale-execution-engine-10

* 基线 SHA：`7d78059d1a6d316d606656759a506d17bc5e3b55`（`origin/master`，2026-09-14 fetch 后确认，本地 master 同步）
* Worktree：`/home/kevin/projects/rs-studio/exp-rs-large-scale-execution-engine-10`
* 分支：`zcode/large-scale-execution-engine-10`（自 `origin/master` 创建，跟踪 origin/master 已解除设置——worktree add 默认带 --track，推送时用 `-u origin` 重设）
* 主机：Linux 6.18 x64 / GCC / Qt6 / GDAL 3.13（与 9.0 track 记录一致的 16C/62GB 级主机）
* 构建入口：`CMakePresets.json`（development preset `build-dev`）；`build.cmd`/`configure_*.cmd` 携带陈旧绝对路径，禁用（D-028）

## 基线时点的执行平面（已验证存在的能力）

| 组件 | 位置 | 状态（基线时） |
|---|---|---|
| TaskCenter | `src/processing/framework/task_center.{h,cpp}`（857+4356 行） | 9 状态生命周期；typed cancel reason；增量 admission heap（8.0 WP-A）；RAM/RSS/profile/global/tempDisk/VRAM/ioHeavy 多维准入；transient auto-retry（0..3）；fused chain；ownership edge（9.0 M1）；execution cache 服务/存储路径（#726）；explainDump（9.0 M7） |
| JobEngine | `src/jobs/job_engine.{h,cpp}`（283+1120 行） | priority buckets；exclusive FIFO；#798 transient workers（kMaxTransientWorkers=8）；ADR 0051 单监听槽；ADR 0052 记录保留策略；sticky shutdown；currentJobId 结构化层级 |
| ExecutionPlane | `src/processing/framework/execution_plane.{h,cpp}` | entry 无关提交面；event-loop 无关 await；commit-once payload |
| LocalWorkerPool | `src/processing/framework/local_worker_pool.{h,cpp}` | 协议 v1；ready 握手健康检查；job 超时；cancel 升级；crash 检测+替换；生命周期回收；QProcess 线程亲和；WorkerProcessGuard（POSIX setsid / Windows Job Object）；heartbeat 能力（8.0） |
| worker_protocol | `src/runtime/worker/worker_protocol.h` | v1 线协议；caps（progress/cancelAck/structuredErrors/outputIdentity/heartbeat）；解析加固 |
| chunk 模块 | `src/runtime/chunk/` | TileSpec（含 halo/bands）；BoundedChunkQueue（有界 MPMC）；ChunkPipeline（线性 producer→stages→consumer，失败传播/取消/进度） |
| gpu_plane | `src/runtime/gpu/gpu_plane.{h,cpp}` | ModelSessionPool；VRAM 预算准入；OOM ladder；fairness；stale 驱逐 |
| observability | `src/runtime/observability/` | trace/trace_id；fault_registry；execution_telemetry；diagnostic_report |
| Execution cache | `src/data/execution_fingerprint.{h,cpp}` + TaskCenter 接线 | V2 fingerprint（chained producer、remote ETag identity、implementation identity、contract version 2）；ExecutionResultCache（LRU 有界、stat 校验自愈、path ownership、持久 content-addressed 池 SICNU_ARTIFACT_CACHE） |
| Workflow checkpoint | `src/workflow/workflow_checkpoint.{h,cpp}` | 原子写（tmp+fsync+rename）；版本门；锁感知恢复；ghost quarantine；history 归档 |
| WorkflowRunCoordinator | `src/workflow/workflow_run_coordinator.{h,cpp}` | 10 状态生命周期；resume；锁外 drain；#860/#876 修复 |
| 资源契约 | `task_resource_budget{,2}.h`、`resource_monitor.*`、`resource_estimation.h` | TaskResourceBudget2 多维（cpu/ram/vram/tempDisk/读写/网络权重）+ latency class + aging + interactive reserve；RSS 采样；溢出安全的 estimate 构造 |
| Operator 能力面 | `src/operators/framework/rs_operator.h` | memoryPolicy（streaming/multipass_streaming/full_raster/external_process/unsupported）；determinism grade（bit_exact/tolerance）；executionEstimate()/estimateExecution(params) |
| tile 生产者 | `src/processing/gdal/gdal_block_stream.{h,cpp}` | 单带、单线程、行主序、float buffer、halo 预复制 |
| streaming 算法基座 | `src/processing/algorithms/image_enhancement_streaming.h` 等 | windowed tile 函数族（Lee/Frost/Kuan/GammaMap/mean convolve…） |

## 9.0 已修问题（不得回归的守护清单）

- #848-#852 P0 修复族（已随 ep9 PR 合入）
- #860 coordinator 锁外 drain；#862 transient child 准入 + worker 线程 wait typed 拒绝；#876 resume ghost 闭合
- #930 TaskCenter/workflow checkpoint IO 不占 scheduler mutex；scratch 输出取消即清理
- #798 worker 子任务等待死锁；#799 instant completion race（submitWithId）；#808 readWindow 内存预算
- IPC framing（#925/#926/#901 部分 frame timeout/unsigned length）；plugin host lifecycle（#908/#917）；锁外 identity resolution（#907）；admission mutex 内不解析 descriptor JSON（#915）；checkpoint IO off mutex（#941）
- ADR 0051/0052（监听槽唯一 + 记录保留）；ADR 0063（RSS 水位）；ADR 0123/0124；ADR 0144（indexed admission、worker containment、fail-closed resume、remote identity）

## 相关 PR/issue 去重

- OPEN PR #973（temporal）、#974（cloud-data-fabric：`src/geospatial/fabric/chunk_plan.*` 为数据编织查询计划，非本 track 的 chunk runtime；无文件级冲突）、#975（scientific-contract-verification）
- OPEN issue #971（全栅 NMS O(n²) 无取消注入点）——本 track 收录其"取消注入 + 有界计算"执行面部分
- OPEN issue #960（offline gate 裸 bool 数据竞态）——`src/data/offline_mode`，data plane 边界；仅在窄修复不越界时处理，否则记录
- OPEN issue #970（.planning 白名单过程缺陷）——本 track 只添加自身白名单条目
