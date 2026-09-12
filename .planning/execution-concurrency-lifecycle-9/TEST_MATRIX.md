# TEST MATRIX — execution-concurrency-lifecycle-9

运行环境：Linux 6.18 x64 / 16C / 62GB / GCC 16.2.1 / Release / Ninja。
所有套件本地可复现；`not built / not run` 明确标注。

## 9.0 新增测试（tests/test_execution_plane_9.cpp）

| 用例 | 里程碑 | 守护不变量 | 旧代码失败方式 |
|---|---|---|---|
| worker-originated child admitted beyond saturated globalMax | M0 #862 | I4 | 子任务滞留 WaitingResource，父任务有界等待超时 → REQUIRE 失败（不挂死套件） |
| waitForTask refuses to park a worker thread | M0 #862 | I4 | 旧代码阻塞 30min → elapsed 断言失败 |
| owner terminal cancels orphaned owned children (I9 join) | M1 | I9 | 旧代码子任务永久 Running → 状态断言失败 |
| cancelling running owner cancels owned children immediately | M1 | I5/I9 | 旧代码无 ownership 边传播 → 子任务不取消 |
| observer slots may re-enter coordinator (#860) | M0 #860 | I3 | 旧代码持锁 emit → 观察者重入自死锁（套件超时暴露） |
| resume swap closes temporary run with terminal broadcast (#876) | M0 #876 | I7 | 旧代码 ghost 无终态 → "sawRunning ⇒ sawTerminal" 断言失败 |
| catalog generation monotonic under concurrent churn (M2) | M2 | 快照契约 | —（8f6293bceb 已修撕裂读；本用例钉 generation 语义 + worker 读合法） |
| pause/resume typed behavior (#702) | M3 | 类型化拒绝 | —（防回归） |
| explain dumps expose admission and run evidence (M7) | M7 | 可诊断性 | —（新能力） |
| deep DAG chain (300) | M8 | 规模 | — |
| cancel storm (60 tasks / 30 cancels) | M8 | I5 | — |
| corrupt checkpoint typed refusal | M6 | I7 | —（钉住既有行为） |
| checkpoint publish fault point（temp→rename 崩溃窗口） | M6 | 崩溃相位恢复 | —（新能力证据） |
| 100k logical admission structures（SICNU_EP9_STRESS=1 门控，SKIP 如实上报） | M8 | I8/规模 | — |

## 既有回归（基线必须全绿，修复后复跑）

| 套件 | 覆盖 |
|---|---|
| test_task_center | #851 回归、准入/取消/DAG/占位符 |
| test_job_engine | #798 transient allowance、桶序、独占、取消 |
| test_worker_host / worker e2e | worker crash/kill |
| test_workflow_run_coordinator | checkpoint/resume/恢复/锁序 #727 |
| test_workflow_resume_provenance | resume 身份/来源 |
| test_execution_plane_7 | 10k 压测、排队取消/关闭矩阵 |
| test_execution_plane_8 | 优先级堆、 scaling、身份戳、远端指纹 |
| test_concurrency_stress | 并发压力 |
| test_temporal_workspace | #852 跨线程撕裂读回归 |

## Sanitizer 计划

- ASan/UBSan/TSan 依据本机可用性执行（ENABLE_SANITIZERS=OFF 的 Release 为主构建；
  sanitizer 构建单独目录，`-j2`，范围限定 execution 相关套件）。能跑多少如实记录。

## 尚未运行 / 环境不支持（PR 中如实声明）

- Windows Job Object 分支：本机 Linux，仅编译级审查（沿 8.0 声明）。
- OTB / ONNX Runtime lane：SICNU_BUILD_OTB=OFF / SICNU_WITH_ONNX_RUNTIME=OFF。
- 线上 CI/CD：不作为完成条件（track 约定）。
