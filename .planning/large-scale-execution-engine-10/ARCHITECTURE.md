# ARCHITECTURE — large-scale-execution-engine-10

## 执行链（不变量，ADR 0144 先例）

```
workflow / agent / cli / gui / mcp
  → WorkflowRunCoordinator（run 生命周期、step checkpoint）
  → TaskCenter（admission：RAM/RSS/slots/tempDisk/VRAM/IO + 【新】tile working-set planner、scratch 预算）
  → JobEngine（priority buckets、exclusive、transient workers）
  → Executor：in-process / LocalWorkerPool（isolated process）/ fused chain
  → OutputCommitter（原子发布）/ ExecutionResultCache（fingerprint 身份）
```

## 本 track 新增组件（全部库级，无第二调度器）

```
src/runtime/chunk/
  tile_spec.{h}          +bandRange/timeIndex（additive）
  chunk_pipeline.*       保留（成为 ChunkGraph 特例）
  chunk_graph.{h,cpp}    【新】多输入 tile DAG：Node(kind: source/stage/join/sink)
                         + N:1 有界输入队列 + 确定性分区（grid spec → tile 序列）
                         + tile lifetime（queue / disk-backed scratch 引用）
  scratch_registry.{h,cpp}【新】scratch 租约：登记、字节预算、引用计数、
                         原子 finalize、崩溃清扫（stale 扫描）、temp 命名
  disk_tile_store.{h,cpp}【新】tile 落盘：自描述头（magic/version/digest）、
                         顺序写、有界写队列（writer backpressure）
  memory_planner.{h,cpp} 【新】tile working-set 规划：
                         WStile = Σbands*(tileW+2h)*(tileH+2h)*bytes
                         WSstream = (stages+1)*queueCap*WStile + in-flight joins
                         → 建议并发/queueCap；超预算 → reduce/spill/refusal(payload 带估算)
  tile_checkpoint.{h,cpp}【新】长任务 tile checkpoint（原子写、版本、身份门、漂移拒绝）
```

## 关键数据流

1. **提交时**：TaskCenter 从 operator 的 `estimateExecution(params)` + 能力声明（tileDependency/halo）推导 TileWorkPlan（memory_planner）→ 与 budget2 维度合并 → WaitingResource/降并发/拒绝（结构化估算进 reason 与 task log）。
2. **执行时**：operator 通过 RSOperatorContext 拿 ScratchRegistry 会话（预算内租约）与 ChunkGraph 运行器；join 节点天然 backpressure；disk_tile_store 的有界写队列节流磁盘。
3. **长任务**：tile 循环每 N tile 落 TileCheckpoint（幂等、版本化）；崩溃后 resume：身份/参数门 → 已完成 tile 从 scratch/disk 复验 digest 复用 → 续跑。
4. **完成后**：scratch 引用计数归零即 GC；checkpoint 移除；OutputCommitter 原子发布不变。

## 不变量（I 系列，沿 ep9 I1-I9 续编）

- I10 任何 tile 中间态要么在 bounded queue，要么在 ScratchRegistry 登记的磁盘对象——不存在第三种"驻留内存的无主大数据"。
- I11 ChunkGraph 任一输入关闭/取消 ⇒ 下游节点有序排空，无死锁（对称于 BoundedChunkQueue 契约）。
- I12 tile checkpoint 只有原子完整态；半写文件被 digest/version 门拒绝（fail-closed）。
- I13 admission 拒绝必须带结构化估算（need/have/建议动作），不允许裸拒绝字符串。
- I14 毒任务升级有界：同一 (operator, error-class) 连续 crash N 次进入 quarantine，需要显式/超时复位，绝不无限 respawn。

（组件逐文件契约在实现 commit 中以头注释为准；本文件只锚定结构。）
