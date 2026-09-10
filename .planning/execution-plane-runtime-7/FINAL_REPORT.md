# FINAL_REPORT — Execution Plane / Worker Runtime / Cache / Recovery 7.0

分支：`feat/execution-plane-runtime-7`（自 master `2041f6fa`）
主机证据：Windows 10.0.26200 / VS2022 / Qt 6.8.0 / Debug / -j2，本地构建+测试。

## 交付总览（对照目标 A-H）

| 包 | 交付 | 关键文件 |
|---|---|---|
| A worker 生产接线 | 选择缝 + 共享池生产化；默认 off = master 行为 | worker_execution_route.{h,cpp}（新）、local_worker_pool、task_center |
| B 资源 admission | tempDisk/VRAM 维度门 + ioHeavy 并发门（默认关）+ never-starve；Windows RSS 采样修复（原为 0=门禁静默失效） | task_center、task_resource_budget2.h、resource_monitor、algorithm_descriptor |
| C 层级取消 | cancel 帧在作业执行期间可送达并被 ack；ack 证据 + 结构化取消码；escalation 梯子修复（宽限期先行） | worker_protocol.h、sicnu_worker_main、pool/host |
| D Crash/Resume | 有界瞬态自动重试（crash/timeout/spawn/send/malformed；上限 clamp 0..3 默认 1；原地复活保 DAG）；per-job 修复（hook 保持、pipeline 状态刷新） | task_center、telemetry |
| E Committer/Provenance | agent plan 两路径全部步骤注册 governed asset + workflow derivation + lineage（P1-E1 关闭）；失败逐 step 可见不伪造 | agent_workflow_executor |
| F Cache 3.0 | serve 后目标尺寸验证（复制 TOCTOU 关闭）；remote identity seam（明确 SEAM ONLY）；磁盘 GC 配额核实已存在（避免重复开发） | task_center、execution_identity_resolver.{h,cpp}（新） |
| G 协议健壮性 | v1 内向后兼容：caps/code/outputs 可选字段 + ack 可选 op；malformed 帧类型化；stderr 环形诊断；progress 透传 | worker_protocol.h、worker_process_io.h（新） |
| H 并发/死锁 | 池析构竞态关闭；spawn/握手/拆除移出池锁（评审 P1）；每-worker 线程亲缘模型；waitForTask 疑点复核为误报（记录） | local_worker_pool |

## 顺带修复的既有缺陷（master 上潜伏）

1. OpenCV 并行后端 INFO 日志写 stdout → worker 帧流去同步（真实算子首次运行即失败，被误报为 timeout）。
2. worker 单线程执行使 cancel 帧不可达——合作式取消从未可能（此前全靠击杀）。
3. Windows RSS 采样返回 0 → 内存门禁整平台静默禁用。
4. worker 复用时 cancelFlag 不复位 → 后继 job 自我取消。
5. QProcess terminate() 在 cancel 帧读取前击杀 → ack/诊断永不产生。

## 测试与证据

- 新套件 test_execution_plane_7：9/9（有界重试×2+DAG 保持、require 路由 e2e、
  fail-closed、tempdisk/VRAM hold+never-starve、RSS parity、admission 下排队
  取消、10k 短任务压测、在飞 shutdown）。
- test_worker_host：12/12（caps/握手/ack 证据/legacy 兼容/崩溃隔离/池生命周期）。
- 回归护栏：8/11；3 个失败为既有 Windows 平台限制（POSIX 只读目录、
  setFileTime 打开句柄语义、QLockFile owner 行格式），非本分支回归。
- 性能记录：10k 短任务 drain 83.3s（Debug，含 TaskCenter admission O(n²)
  提交扫描——记录为已知特性）；worker 冷/热启动、缓存 hit 延迟等沿用
  benchmarks/execution-bench 既有基线（本分支未改变其数据面）。

## 兼容性

- 默认配置下生产行为不变：worker 路由 off、admission 新维度 off、自动重试
  仅命中 worker 基础设施错误前缀、wire 不升版（旧 host/worker 组合行为不变，
  逐字节核对新旧互操作）。
- checkpoint/缓存/登记既有格式不变。

## 已知限制与后续事项

1. identity resolver seam 已落地但未接入指纹收集器（SEAM ONLY，激活远程身份
   缓存为后续）。
2. Windows Job Object 未引入：host 进程死亡时 worker 依赖 stdin-EOF 契约退出；
   卡死算子可能残留孤儿进程（不影响 GUI 稳定性）。
3. 跨线程拆除 idle worker（有界等待）超出 Qt 文档契约但互斥成立；完整
   owner-thread marshal 为后续。
4. image_fusion GUI/CLI 直连路径未走 committer（#617 残留，防截断 guard 已有）。
5. moved-output / changed-operator resume 未实现（现有 resume 身份门保持
   fail-closed：路径不符即重跑，不伪造成功）。
6. TaskCenter 提交扫描 O(n²)：10k 任务提交约 80s（Debug），为既有 admission
   设计特性，压测已覆盖且无 stranded。
