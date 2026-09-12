# PERFORMANCE — 9.0 执行面

记录规范：每次 benchmark 标注 host（Linux 6.18 x64 / 16C / 62GB）、构建类型（Release/Ninja）、
数据规模、资源上限（-j）。Debug 与 Release 数字不互比。

## 复杂度契约（静态上界）

| 路径 | 复杂度 | 说明 |
|---|---|---|
| 准入 pass（含 9.0 globalMax gate 内移） | O(examined · log n)，examined ≤ max(32, 4·globalMax)；饱和且无 bypass 容量时 O(1) break（review B-P3-1） | RSS 探测每 pass 一次；hold 的 pass 最多弹出 scanBudget 项并原样重排 |
| transient child 判定 | O(1)（不可变标志 + 原子计数器维护） | setTaskStatusLocked 进出活跃集时维护 |
| owner 解析（submit 时） | O(log n)（m_taskByJobId map 查询） | 每 submit 一次 |
| join 级联（owner terminal） | O(owned children) 摊销 + 既有 O(V+E) 后代收集 | staging 只入队；引擎交互在锁外 |
| 通知队列 drain | O(queued) | swap 出锁外逐条 emit；队列上界 = 每次迁移 1 条 |
| explainDump | O(all retained tasks)（m_tasks 全图状态计数）+ O(1) 格式化字段 | 诊断专用非热路径；临界区内全图扫描为有意取舍（review A-F1/F9、B-P3-2 已记录） |

## 9.0 新增热路径成本

- enqueueTask：+`JobEngine::isWorkerThread()`（thread_local 读）+ worker 情形下一次 map 查找。
- setTaskStatusLocked：terminal 迁移时 +1 次 RSS 采样（review B-P2-1 后以 Trace::enabled() 门控——
  tracing 关闭时零 /proc 解析）。
- waitForTask/Pipeline：worker 线程早退（比旧路径更便宜）。

## Benchmark 计划（构建完成后回填数字）

- ep7 10k 短任务 drain（既有，回归对照）。
- ep9 300 深链 drain。
- ep9 cancel storm 收敛时间。
- SICNU_EP9_STRESS=1：100k 逻辑任务 enqueue+dump 耗时与 RSS 上界。

（回填区：构建完成后执行并填入实测数字与命令行。）
