# ARCHITECTURE — Execution Plane 7.0

## 权威数据源与唯一执行缝

- 唯一调度链：`WorkflowRunCoordinator -> TaskCenter -> JobEngine -> Executor`。
- Executor 是执行**位置**的抽象（job_engine.cpp:762-790 解析链），不是第二调度器。
  7.0 的新执行位置（隔离进程池）只以 Executor 形态挂入既有解析链。
- 输出发布唯一权威缝：OutputCommitter（output_committer.h:66）。所有成功产出的
  workflow/agent 路径必须经它（或记录在案的 ADR 0023 CLI 例外）。
- 缓存身份唯一权威：ExecutionFingerprint（contract v2+）。

## 不做什么

- 不新建第二个 scheduler/job graph；不把 LocalWorkerPool 升级成任务队列。
- 不把 worker 进程变成通用计算代理：只执行 RSOperator（现 sicnu_worker 契约）。
- 不重建 QGIS 渲染/缓存；不碰 src/core 的 Qgs*cache（那是地图渲染域）。
- 不为 GUI 增加科学计算逻辑。
- 不做无限自动重试/自动修复；一切有界、可解释、可取消。

## 关键设计决策

### D1. Worker 执行路由（A 包）——Executor 选择策略
- 新组件 `IJobExecutor`（沿用 JobEngine 既有 executor 概念）+ 路由策略
  `WorkerRoutingPolicy`：
  - `InProcessOnly`（默认，行为与 master 完全一致）
  - `IsolatedPreferred`：operator 声明 `executionHint: isolated` 或 provider
    profile 要求时走 LocalWorkerPool；进程池不可用/握手失败 → **fail-closed 上报，
    不静默回落**（防止"以为隔离了其实没有"），除非策略为 `IsolatedWithFallback`。
  - 配置入口：环境变量 `SICNU_WORKER_EXECUTION=off|auto|require`（默认 off，
    生产行为不变）+ TaskCenter API 覆盖。
- 池并发与 JobEngine 线程占用：worker 路由的 job 在 engine 线程上等待子进程，
  属"长 operator"语义；由新 admission 维度 `externalProcess` 限流（默认 2），
  防止 engine 线程饥饿。
- 池增补：generation id（每次 start 递增；旧代 worker 一律退役）、健康快照
  入遥测、崩溃主动补充（下一代预热）、优雅停机挂接 TaskCenter shutdown。

### D2. 协议扩展不升级 wire version（G 包）
- 保持 `v==1` 硬语义不变；一切新增为**可选字段/可选 op**：
  - `ready` 帧增加可选 `caps:["progress","cancelAck","structuredErrors","outputIdentity"]`。
  - `result` 帧增加可选 `outputs:[{path,sha256,sizeBytes}]`（输出身份）。
  - `error` 帧增加可选 `code`（结构化错误枚举字符串）。
  - 新 worker→host op `ack {jobId, kind:"cancel"}`；旧 host 忽略未知 op
    （parseFrame 放宽为"未知 op 可解析、由调用方忽略"，这是向后兼容的方向：
    老 worker 无新字段照常工作；新 worker 对老 host 的多余帧被忽略）。
- stderr：host 侧环形缓冲收集（每 worker 上限，如 64KB），崩溃/超时时附加到
  诊断（telemetry 事件 + typed error text），不改变 wire。

### D3. 统一资源 admission（B 包）
- 以 TaskResourceBudget2 的维度模型为准绳，但在 TaskCenter 落地为**增量接线**：
  - 新 `AdmissionDimensions { ramMb(已有), cpuSlots, vramMb, tempDiskMb,
    externalProcesses, ioHeavy }`。
  - 维度来源优先级：operator manifest/algorithm descriptor 显式 hints →
    估算解析器默认 → 全局缺省。
  - GPU VRAM：向 GpuPlane/ModelSessionPool 查询每设备剩余预算（只读查询，
    不改变其所有权）；无 GPU 时维度为 0 且不阻塞。
  - 有界性：任一维度的 gate 只 DELAY 不拒绝（沿用 never-starve：全空时放行）；
    ioHeavy 类并发上限独立计数。
  - Windows RSS 修复：resource_monitor 增加 Windows 实现
    (`GetProcessMemoryInfo`)，消除"门禁静默失效"。

### D4. 层级取消（C 包）
- 契约：每层只向**直接下层**传播取消，并要求**终态证据**（ack/exit/flag）：
  - workflow→task：现有 cascade 不变。
  - task→executor：现有 arming 不变。
  - executor→worker：cancel 帧后等待 `ack` 或退出；超时 escalate
    terminate→kill（现有梯子）+ 遥测 `CancelEscalations`。
  - operator→tile/batch：chunk_pipeline flag 已有；补 batch 边界检查点惯例。

### D5. 有界重试类（D 包）
- `RetryClass { None, Transient, Idempotent }`：
  - worker crash/timeout = Transient：自动重试上限 `maxAutoRetries`（默认 1，
    env `SICNU_TASK_MAX_AUTO_RETRIES`，硬上限 3），指数退避；重试计任务日志。
  - operator error / validation = None：绝不自动重试（部分副作用不可知）。
  - 只有声明 `idempotent: true` 且输出走 committer 的任务可 Idempotent（上限同）。
- resume 增强：
  - moved output：resume 身份门先查绝对路径；未命中再按 artifact identity
    （size+mtime+digest）经 DataManager/ArtifactStore 定位迁移后的路径，命中
    则更新 run 记录（fail-closed 语义不变）。
  - changed operator：checkpoint 记录每步 operator definition hash；resume 时
    不匹配 → 该步及下游重跑（fail-closed），并计入 run 报告。

### D6. Committer/Provenance 补缝（E 包）
- agent plan 步骤与 image_fusion 走 OutputCommitter；cache-serve 路径产出
  附带 verification status（digest 验证结果）并写入 DerivationRecord/
  governed 注册的 verification 字段（unknown 必须显式为 unknown）。

### D7. Cache 3.0（F 包）
- 磁盘 GC：持久池 size 配额（env `SICNU_ARTIFACT_CACHE_MAX_GB`，默认 20GB，
  0=off），LRU-by-last-verify，回收跳过被引用对象（沿现状）；TaskCenter 结果
  map 维持条目上限。
- post-serve 验证：serve 后对 declared output 打开+头部抽验（格式魔数），
  失败→自愈擦除+重跑。
- remote identity adapter seam：`IExecutionIdentityResolver` 接口（本地默认
  实现=现行为），为 remote 输入的稳定身份留扩展点，不实现远端。

### D8. 并发修复（H 包）
- waitForTask/waitForPipeline：wait 循环改为分片等待（有界片），防止唤醒间
  长持锁；语义不变。
- 池接线后的 shutdown 顺序：TaskCenter shutdown → 池 shutdown（quiesce）→
  JobEngine sticky shutdown；进程不因 GUI 关闭而崩（QProcess 父子天然隔离 +
  EOF 契约 + kill 兜底）。

## 模块所有权

- `src/processing/framework/`：TaskCenter/池接线/admission/committer 缝。
- `src/jobs/`：JobEngine executor 契约（最小改动）。
- `src/workflow/`：checkpoint/resume/重试类。
- `src/runtime/worker/`：协议；`src/runtime/observability/`：遥测新事件。
- `src/data/`：fingerprint/GC/identity seam。
- `src/cli/sicnu_worker_main.cpp`：worker 侧协议能力。

## 兼容性

- 默认配置下全部新路径关闭/旁路（`SICNU_WORKER_EXECUTION=off`、cache 默认关、
  重试默认 None→Transient 上限 1 仅对 crash/timeout）。
- wire 不升版；旧 worker/旧 host 组合行为不变。
- checkpoint 旧格式继续可读（legacy → 重跑，现契约保持）。
