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

## 实测记录（2026-09-13，build-fabric10 Debug，GCC 16.2.1，本机 loopback）

* P-1 100k 记录 planFabric（sceneBudget=8，流式 Top-K）：进程峰值 RSS 增量 ≈ **1.0 MiB**
  （独立探针实测 before/after ru_maxrss：93208KB → 94276KB）——契约 O(page + selected)
  成立；作为断言固化在 `test_io_fabric_scale`（< 2 MiB）。
* P-2 百万 chunk：64 time × 128×128 spatial = **1,048,576** 逻辑 chunk——`chunkCountTotal()`
  即时；`materializeChunks(total-5, 100)` 返回 5 条；plan JSON < 256 KB（测试断言）。
* P-3 大逻辑窗口：4096² 逻辑网格（16.7M 格）上 512² 窗口读：RSS 增量 < 16 MiB
  （窗口本身 2 MiB doubles）（测试断言）。
* P-4 prefetch：loopback 本地资产全 cache-hit（0 origin bytes）；真实字节量路径由
  /vsis3/ loopback 集成测试覆盖（bytesServed 有界、window 读取精确）。
* 内存数字为 evidence，不是 gate；断言阈值含 2× 以上余量。

（待填实际数据）
