# BENCH_BASELINE — R27-T2

本轨目标为可靠性与可恢复性，非吞吐性能；E 里程碑的验收是**精确性与资源有界**，不是 wall-time 提升。测量均在本机（负载 1.6–5.3 波动、兄弟轨并行编译），只做计数型/上界型断言，不宣称性能提升。

## 计数型 Oracle（无 wall-time 依赖）

| 指标 | Oracle | 结果 |
|---|---|---|
| 尾批像元守恒（100×60 @ 32×32，尾 4×28） | Σ tile pixels == 100×60，字节级 == truth | 通过（E 用例，双跑） |
| 断点续算 | 注入 journal 失败后 compute 次数 == totalTiles+1（恰好 1 块重算） | 通过 |
| 取消恢复 | 3 块提交后取消 → resume reuse==3、compute==total−3 | 通过 |
| 租约有界 | 500 代 recycle 后 trackedCount ≤ live 上界（反例：不 reset == 500） | 通过 |
| 真实 pool | 4 代 recycle、totalRecycles≥3、trackedLeases ≤ maxWorkers，shutdown 后 ==0 | 通过 |
| 有界 shutdown | stuck worker 下 elapsed < 5s（注入 deadline 250ms），stuck ≥ 1 | 通过 |
| mirror 页重复 | 重复 continuation 立即停止（新用例经 RIO-13 附加验证） | 通过 |

## 资源峰值（定性核验，非精确 profiler 数据）

- chunk graph/join 路径：queueCapacity 有界（F-A-10 既有），本轨未扩容。
- JobEngine：m_workers 只增 spawn 于 allowed 内、shutdown detach 不累积 join；m_jobDeadlines 终态即清（finishJobLocked + 双 shutdown）。
- 租约表：上界 = 并发 worker 数（修复核心）。
- ResumableTileRun：O(committed bitmap + 1 tile) 不变；新增一个 fd（锁文件）每 run。
- audit_log：打开时截断至最新 100k 行（上界）。

## 未测项（如实声明）

- 256/512/1024 多 tile 尺寸的大栅格吞吐矩阵、远程连续/随机读取延迟、磁盘满故障注入：本轨未做性能基准对比（非本轨 Oracle 必需项；E 验收以尾批/取消/注入故障的语义 oracle 落实）。NOT_EXECUTED。
- GPU provider CPU fallback 资源峰值：未在本轨实现（GPU plane 属既有能力，#1765/1767 不触及）；NOT_EXECUTED。
