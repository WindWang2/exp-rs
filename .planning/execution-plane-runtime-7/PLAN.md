# PLAN — 实施顺序与验证门

原则：先 contract 后功能；小步 commit；每里程碑有可重复本地证据；
默认配置下生产行为不变。

## M0 基线（当前）
- worktree + 档案 ✓
- [ ] 本地 configure + 构建 sicnu_processing/相关测试目标，跑既有执行面套件取绿基线。
- 证据：ctest 结果记录到 MILESTONES.md。

## M1 (G) worker protocol v1 向后兼容扩展
- worker_protocol.h：可选字段 caps/code/outputs、可选 op ack；parseFrame 放宽
  （未知 op 可解析，调用方忽略）；版本仍硬 v==1。
- sicnu_worker_main：发 caps、cancel ack、error code、result outputs。
- local_worker_host/pool：收集 caps、处理 ack、stderr 环形缓冲诊断。
- 测试：test_worker_host 扩展（caps 协商、ack 竞速、stderr 捕获、旧协议帧兼容）。

## M2 (A) LocalWorkerPool 生产接线
- LocalWorkerPool：generation、健康遥测、主动崩溃补充、优雅停机 API。
- JobEngine/TaskCenter：executor 解析链挂入 worker-route executor；
  `SICNU_WORKER_EXECUTION=off|auto|require`；路由策略按 provider profile +
  operator manifest `executionHint`。
- admission 维度 `externalProcess` 限流（默认 2）。
- TaskCenter shutdown 顺序挂接。
- 测试：路由选择、require 下不可用 fail-closed、crash 替换、GUI 不受 worker
  crash 影响（进程隔离既有性质 + 新回归测试）、rapid jobs 过池、shutdown quiesce。

## M3 (B) 统一资源 admission
- AdmissionDimensions 接线 TaskCenter（ram/cpu/vram/tempDisk/extProc/ioHeavy）。
- manifest hints：operator descriptor + algorithm descriptor 增加
  资源提示字段（可选，缺省回退现状）。
- resource_monitor Windows RSS 实现。
- 测试：多任务 OOM 防护（小数据构造大估算）、ioHeavy 限流、VRAM 查询、
  never-starve 不回退、Windows 采样>0。

## M4 (C) 层级取消加固
- cancel 帧后等待 ack/退出；escalation 遥测；取消终态证据链路。
- 测试矩阵：queued/running/child-job/worker-hung/external-process 各态取消。

## M5 (D) Crash/Resume 3.0
- RetryClass + 有界自动重试（crash/timeout, maxAutoRetries≤3, 退避, 日志）。
- worker 崩溃部分产物清理（temp 前缀约定 + 清扫）。
- moved-output resume（identity 定位）；changed-operator resume（definition hash）。
- 测试：重试上限恰好 N、永久错误不重试、resume 三个新场景、部分产物不伪造成功。

## M6 (E) Committer/Provenance 补缝
- agent_workflow_executor 步骤走 committer（P1-E1）；image_fusion 补缝。
- cache-serve 输出 verification status 持久化（unknown 显式）。
- 测试：各路径产出带 DerivationRecord/verification 字段。

## M7 (F) Cache 3.0
- 持久池 size 配额 GC（LRU-by-last-verify、跳过被引用）。
- post-serve 打开验证；IExecutionIdentityResolver seam（本地默认实现）。
- 测试：配额回收、被引用不回收、corrupt 输出自愈重跑、seam 注入。

## M8 (H) 并发/死锁修复
- waitForTask/waitForPipeline 分片等待；shutdown 顺序竞态测试；listener
  生命周期审计结论记录。
- 测试：压力下 shutdown 无挂死、无 stranded 任务。

## M9 故障矩阵 + 压力 + 性能
- TEST_MATRIX.md 12 场景全部落地（小数据、高压）。
- 性能记录：dispatch latency、worker cold/warm start、throughput、RSS、
  cache lookup、resume latency、10k short jobs、long job cancel → benchmarks/*.json。

## M10 Review + 集成 + PR
- subagent #2 adversarial review → 修 P0/P1 + 合理 P2 → 同步 master → PR。
