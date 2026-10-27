# PERF_BASELINE — r23-C

本轨修复均为正确性/契约修复，无吞吐优化目标；性能记录以「不引入回归」为验收。

## 基线（本机实测，GCC 16.2.1 Debug，build-r23-C）
- 基线套件直跑（修复前工作树，seam 中性）：
  - `./tests/test_task_center`：533 assertions / 34 cases，全绿（ wall ~38s，含多线程用例等待）
  - `./tests/test_r22_agent_job_logs`：24031 assertions / 4 cases，全绿
- 配置：`cmake -S . -B build-r23-C -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON` exit 0（27-30s 生成）

## WP-1 影响面评估（修复前后对比计划）
- 消费端每条 cumulative 记录新增 O(1) 算术（max/加法），无新分配；delta 分支不变。
- 有界性继承 #1705（kMaxJobLogLines=10000 / kJobLogKeepLines=5000；TaskCenter 环 2000/4000，#1454）——修复不改变任何上限。
- 度量：GREEN 后以 test_r23_c_task_center_log_spaces 全量断言耗时 + test_task_center 全套件回归耗时对照基线（数量级相同即无回归）。

## WP-2 影响面评估
- 新增一次 staging I/O（两载荷各多一次同目录临时写+rename）+ journal 写；批量报告为小文件（KB 级），耗时增量可忽略；正确性收益=成对原子。
- 度量：GREEN 后 test_r23_c_teaching_pair_publish 三用例耗时记录。

## 资源约束遵守
- 全程 -j2 + 全局 flock（与 r23-A/r23-B 并行轨互斥）；实测排队等待即证明互斥生效。
- 无 OOM/ENOSPC 事件（监控点：free -g 可用 ~45G 恒定）。
