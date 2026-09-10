# TEST_MATRIX — 故障矩阵（12 场景）+ 压力 + 性能

目标：全部场景有自动化测试（小数据、高压、可重复）；标 (W) 的必须在本
Windows 主机可运行（不依赖 POSIX 专有设施）。

| # | 场景 | 覆盖测试（新/既有） | 状态 |
|---|---|---|---|
| 1 | worker crash（握手后/运行中） | test_worker_host 既有 + 新：崩溃后替换 worker 接续服务、重试类计数 | |
| 2 | worker timeout/hang | 既有 `__hang__` 钩子 + 新：escalation 遥测、重试上限 | |
| 3 | cancel（queued/running/child/external） | 既有 test_task_center/test_workflow_cancel + 新：worker cancel ack、external-process 取消 | |
| 4 | disk full（cache/store/commit 写失败） | 新：可注入的写入失败钩子（配额模拟），committer 事务回滚 | |
| 5 | output corrupt | 既有 #750/#749 系 + 新：post-serve 打开验证自愈 | |
| 6 | input changed | 既有 out-of-band 重写用例 + 回归保持 | |
| 7 | cache stale（operator schema/contract 升级） | 新：contract version 升位后跨运行全 miss | |
| 8 | resume identity mismatch | 既有 digest 验证 + 新：changed-operator/moved-output | |
| 9 | child-job deadlock | 既有 #798 线程池 + 新：worker 路由下子任务等待拒绝路径 | |
| 10 | rapid jobs（10k short jobs） | 新 stress：小任务洪峰过 TaskCenter+JobEngine（计时+无 stranded） | |
| 11 | app shutdown（在飞任务/worker） | 新：TaskCenter shutdown + 池 quiesce 竞速，无挂死无崩溃 (W) | |
| 12 | queued cancel（admission 风暴下） | 新：RSS/RAM hold 风暴中排队取消即时终态 | |

## 性能记录（benchmarks/*.json, schema execution-bench/1）
- dispatch latency（submit→Running、submit→terminal，小任务）
- worker cold start vs warm start（进程 spawn+握手 vs 池复用）
- throughput（10k short jobs 总时长）
- RSS（运行前后进程 RSS，Windows 采样修复后）
- cache lookup（hit/miss 均值）
- resume latency（N 步 pipeline 全缓存 resume）
- long job cancellation latency（cancel→terminal）

## 回归护栏（不得回退）
test_job_engine / test_task_center / test_execution_plane / test_worker_host /
test_workflow_run_coordinator / test_workflow_recovery / test_workflow_cache_e2e /
test_execution_fingerprint / test_output_committer / test_scheduler3 /
test_task_resource_budget / test_gpu_plane / test_workflow_cancel
