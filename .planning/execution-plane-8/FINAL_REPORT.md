# FINAL REPORT — Execution Plane / Worker Runtime / Admission / Cache & Recovery 8.0

分支：`feat/execution-plane-8`（自 `origin/master` = `322dfd3876`）
主机证据：Linux 6.18 x64 / 16C / 64GB / Qt 6.11.2 / Release / Ninja，本地构建+测试，无线上 CI 依赖。

## 交付总览（对照 track 包 A–I）

| 包 | 交付 | 关键位置 |
|---|---|---|
| A admission 复杂度 | TaskCenter 增量 active 计数（唯一状态迁移缝 `setTaskStatusLocked`）+ 就绪堆 `(priority, epoch, taskId, serial)`（懒失效）+ 每轮有界扫描 + 暂存期才做占位符/指纹校验；JobEngine 优先级桶 + 独占 FIFO（pick O(log P)）。语义保持：新鲜候选严格 `(priority, taskId)`、never-starve、独占 drain-then-alone、WaitingResource 可观测性 | task_center.{h,cpp}、job_engine.{h,cpp} |
| B 资源感知 3.0 | 全部资源限额 setter 触发即时重准入（动态可用性）；预留=Dispatching 计入 active、终态释放；排队取消路径（ep8） | task_center.cpp |
| C worker 生命周期 | `worker_process_guard`：POSIX setsid 进程组 + 组级 SIGTERM→SIGKILL 阶梯（leader 干净退出后仍清组）；Windows kill-on-close Job Object（start 后即挂载，逃逸窗口已如实记录）；worker 心跳帧（15s、wire 兼容）+ 主机 hang 窗口（默认关） | worker_process_guard.{h,cpp}、local_worker_pool/host、sicnu_worker_main、worker_protocol.h |
| D 重试语义 | 瞬态分类器升为公开文档缝；重试尝试/类别/预算耗尽证据进日志 + trace | task_center.{h,cpp} |
| E Resume 3.0 | StepPlan.operatorImplStamp（算子实现身份：schema+grade+contract+platform，完成时落盘）；resume 门：身份不符/不可证 ⇒ 重执行（fail-closed，与 #750 同方向）；moved-output：池对象摘要验证后重水化，stat 身份收敛 | workflow_run.{h,cpp}、workflow_run_coordinator.cpp、execution_fingerprint.{h,cpp} |
| F Cache identity | 远端 http(s) 输入经强 ETag 身份（`remote_identity_resolver`，Qt-free、有界会话缓存、TTL）；默认由 TaskCenter 安装（宿主覆盖保留）；采集器在目录不可解析时先询问 resolver；`remoteIdentity` 为规范化序列化的增量可选字段（旧指纹字节不变） | geospatial/remote/remote_identity_resolver.{h,cpp}、temporal_workspace.cpp、execution_fingerprint.{h,cpp} |
| G Committer 收敛 | 全入口审计：7.0 的 image_fusion 直连限制已被中间工作解决（`runImageFusion` 无生产直连调用方）；GUI 对话框产物是否入目录为 workbench 治理策略，记录为跨轨后续 | CAPABILITY_MATRIX.md |
| H 锁审计 | 网络零锁内（身份探测 warm-before-lock + TTL）；新增状态全部 m_mutex 内；guard kill 无锁；评审发现的 m_tasks 越锁访问已修 | REVIEW_LOG |
| I 可观测性 | admitted/held/retry/cancel/terminal/cache（TaskCenter）+ resume served/rehydrated/operator_changed（协调器）trace 事件，关闭时一次 relaxed load | task_center.cpp、workflow_run_coordinator.cpp |

## 验收对照

- **不再有 10k 短任务 O(n²) 悬崖**：本机 Release 下 ep7 10k 压测 4.8s；ep8 比例断言（2k vs 10k，best-of-3，<10×）常开。
- **排队取消与资源准入竞态安全**：ep8 取消测试 + ep7 排队取消/关闭矩阵全绿；计数收敛由状态缝单点维护。
- **worker crash/hang/restart/shutdown 不留孤儿**：POSIX 组级击杀 e2e（SIGTERM 免疫 helper 被清）；Windows Job Object 关闭即清树（本机仅编译验证，PR 如实声明）。
- **resume/cache 跳过决策证据化、fail-closed**：操作符身份戳 + 摘要重水化；旧 checkpoint 保守方向；ep8 身份门 e2e。
- **权威输出单一发布契约**：审计矩阵确认（G）。
- **未引入新调度器**：堆/桶为两个既有调度器的内部结构。

## 测试（全部本地、可复现、有界）

| 套件 | 结果 |
|---|---|
| test_execution_plane_8（新：优先级/取消/DAG/重试/桶序/scaling/动态限额/身份戳/算子变更 resume/远端指纹/默认 resolver/包含树击杀 e2e） | **105 断言 / 13 用例全绿** |
| test_task_center / test_job_engine / test_worker_host | 342+446+56 断言全绿 |
| test_execution_plane_7（含 10k 压测 4.8s、worker e2e、fail-closed 路由） | 48 断言 / 9 用例全绿 |
| workflow_run_coordinator / resume_provenance / cache_e2e / fingerprint / scheduler3 | 171+67+189+60+11 断言全绿 |

## 对抗评审

两位独立只读评审（架构/正确性 + 测试/性能/可移植性）。合并发现：
**2×P0（同一死锁根因）、6×P1、6×P2、8×P3** — 全部修复，含评审后本地
验证又抓到的一个 P0 级回归（isolatedRoute 计数时序），修复后全绿。
明细与处置见 `REVIEW_LOG.md`。评审"不可合并"结论已解除。

## 兼容性

- 默认行为不变：心跳默认不发不改语义（旧主机忽略未知 op）；hang 窗口默认关；
  执行缓存默认关 ⇒ resolver 零网络；checkpoint 新字段可选、旧读者不受影响。
- 旧 checkpoint 含"当前可解析算子"的已完成步骤将重执行（fail-closed，与
  #750 先例同方向）；前缀执行器/provider 步骤（注册表不可解析）维持可服务。
- 指纹契约版本不变（`;rid=` 仅对使用它的输入出现）。

## 已知限制与后续

1. Windows Job Object 分支本机仅编译级审查（Linux 主机），挂载窗口
   （start→arm）已文档化；建议 Windows lane 跑一轮 ep7/ep8。
2. GUI 对话框产物目录注册策略属 workbench 治理轨（未改）。
3. 模型产物摘要进指纹属 model-runtime 轨的描述符缝（未改）。
4. 未知估计（estimate=0）在预算开启时不拦截 — 维持既有保守回退策略，
   如需收紧为"拒止"属调度策略变更，建议单独立项。
