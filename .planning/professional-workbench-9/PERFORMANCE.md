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
| 200k 索引过滤 + 全扫（8.0 契约保持） | Release ci-fast | 200k 合成记录 | 过滤 <2000ms（内部断言）；套件总墙钟 | median 1.17s（3 次取中位，本机） |
| 分页翻页（M7） | Release ci-fast | 12 资产 / 3 页来回翻 | 套件总墙钟。注意：每次翻页 = 一次完整 coalesced refresh（O(catalog 过滤一遍 + 渲染行 ≤ cap)），非增量插入——与 REVIEW_LOG R1-5 一致 | median 0.51s（含进程启动） |
| 工程 churn stress（M0） | Release ci-fast | 48 轮 clear/import/视图/先关窗 | 套件总墙钟 | median 1.25s |
| enum provider 上限（M6） | Release | >200 选项源 | kMaxChoices=200 截断 + 截断标注 | test_workbench_enum_provider 断言锁定 |

环境：本机 Arch linux（6.18 LTS）、GCC 16.2.1、GDAL 3.13.3-2、独立 worktree、
无其他重型负载并发。所有数字为 Release（ci-fast）；未与 Debug 混比。
复杂度注记：分页切片 O(window)；refresh 依赖 8.0 的 coalesced timer +
AssetCatalogIndex 增量过滤（O(assets) 单遍），未引入新的全量复制或 O(N²) 路径。
