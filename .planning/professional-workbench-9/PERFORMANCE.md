# PERFORMANCE — 纪律与证据

## 原则

- 先 baseline 后优化；hot path 给复杂度与上界。
- cache/queue/worker/pool 必须有容量、淘汰/回压、统计、失败行为
  （RsScanPool=2 workers；preview LRU=64/32MiB；catalog 渲染 20k 行 sentinel）。
- 优化不得改变科学结果或稳定契约。
- 禁止 Debug/Release 混比；每条记录标注 build type、机器负载、并行度。

## 资源上限（本方向执行纪律）

- 构建 `-j2`（默认）/`-j4`（上限）；ctest `-j1`；不与他 worktree 重型构建并发。
- 测试数据逻辑规模有上限；scale 测试用合成索引/合成栅格，不落实体大数据。

## 证据记录（随 milestone 追加）

| 项 | 构建 | 数据规模 | 指标 | 结果 |
|---|---|---|---|---|
| （待 M7：200k catalog 分页 vs 全量） | Release ci-fast | 200k 合成记录 | 分页响应 <50ms/页，RSS 上界 | 待测 |
| （待 M6：enum provider 上限截断） | Release | 100k datasets 合成 | provider 首屏 ≤N 条，无全量物化 | 待测 |
