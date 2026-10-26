# R22-04 (r22-agent) PERF_BASELINE

## 有行为度量的改动

### #1705 JobEngine 日志有界化（唯一有量化 oracle 的改动）

- 上界（修复后常量）：单行 ≤ 4 KiB（UTF-8 安全截断 + 标记）；保留窗口 ∈ [5 000, 10 000] 行；最坏驻留 ≈ 10 000 × 4 KiB ≈ 40 MiB/作业（此前无界：issue 复现 50 000 行全保留；25000 行用例 master 保留 25 002 行 vs 修复后 10 000 行 + droppedLogLines=15 002 精确记账）。
- 通知复杂度：逐行 delta O(本次新增行) 不变；snapshot/终态复制从 O(无界总量) 收敛为 O(保留窗口 ≤10 000)。
- 逐行驱动代价：块驱逐（一次 erase 5 001 元素）发生在跨过 10 000 边界时，摊销 O(1)/行。
- delta 协议：绝对索引（droppedLogLines + local）保证消费端（TaskCenter m_forwardedLogCounts）单调、exactly-once——由 test_r22_agent_job_logs 的连续性断言（24 031 断言族）验证，非口头结论。

### 其余改动

- #1704/#1690/#1691/#1692/#1693：正确性/安全修复，无热路径算法变更。#1704 分类为每字符串参数一次的 O(prefix) 判定；#1692 仅在 >1 MiB 行的异常路径增加一次 256 B 正则扫描。
