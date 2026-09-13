# PERFORMANCE — cloud-data-fabric-datacube-10

政策：性能数字是 evidence，不是 gate；不写 wall-clock 脆弱阈值。热点先 profile
再优化。

## 记录格式

```
Bench <名> | 输入规模 | 指标（RSS 增量 / 计数 / bytes） | 命令 | 运行环境
```

## Phase 5 计划基准

* P-1 catalog 100k 记录 → planFabric：进程 RSS 增量 ≈ O(sceneBudget) 而非 O(100k)。
* P-2 chunk plan 百万级（如 4096×4096×512×4 逻辑格）→ count 即时、materialize(0, 1024)
  有界。
* P-3 虚拟立方体 10k 资产索引构建 + 1024² 窗口查询延迟量级（evidence 记录）。
* P-4 prefetch 1024 chunk × 64KiB 块：bytesFetched == 预算收敛、取消即时生效。

（待填实际数据）
