# PLAN — cloud-data-fabric-datacube-10

对齐 GOAL.md 九阶段预算；每个 Phase 的退出条件是可验证的。

## Phase 1 — 契约与架构（WP-A，48M）
1. 逐字落定 `ARCHITECTURE.md` 契约为 `src/geospatial/fabric/*.h` 头文件（只头文件 +
   空实现注册，保证 CMake 可配置）。
2. DECISIONS.md 写 ADR 级决策（D-10xx 系列）。
3. 退出：`cmake` configure 成功；头文件自包含（编译一个翻遍全部头的翻译单元）。

## Phase 2 — Foundation：object store + catalog service（WP-B/C，54M）
1. `fabric/object_store`：profile 表、URI 解析、ScopedObjectStoreCredentials、offline
   拒绝、`/vsirangecache/` 拼写整合。
2. `fabric/catalog_service`：三后端 + 统一查询 + 分页 + cancel + 统计。
3. 测试 `test_io_fabric_object_store`、`test_io_fabric_catalog`（loopback STAC fixture
   复用 + 本地临时 STAC 树 fixture）。
4. 退出：两个测试目标绿。

## Phase 3 — 虚拟立方体 + chunk 计划（WP-D/E，46M）
1. `fabric/virtual_cube`：索引、网格协商、overlap/quality policy、readWindow + provenance。
2. `fabric/chunk_plan`：命名维、切片、有界物化、总数计数。
3. 测试 `test_io_fabric_cube`（synthetic_raster_builder 造场景集）、`test_io_fabric_plan`
   起步（chunk 计划部分）。
4. 退出：测试绿。

## Phase 4 — planner + cache 10 + 集成面（WP-F/G/H，38M）
1. `fabric/query_planner`：intent→plan（流式页消费）、executeFabric、报告。
2. `fabric/prefetch`、`fabric/mirror`。
3. 算子 4 个 + CLI 子命令 + `docs/io/fabric-10.md` 起步。
4. 测试：planner（含取消/预算）、prefetch/mirror（loopback range server）。
5. 退出：`test_io_fabric_plan`、`test_io_fabric_cache`、`test_io_fabric_operators` 绿。

## Phase 5 — 规模与性能（WP-I，34M）
1. `test_io_fabric_scale`：100k 记录计划（页流式，内存断言）、百万 chunk 计数、
   取消延迟。
2. `PERFORMANCE.md` 记录证据（process RSS 采样、运行次数中位数）。
3. 退出：scale 测试绿 + 证据落档。

## Phase 6 — 失败/边缘矩阵（WP-I 收尾，22M）
1. 空/单资产/all-NoData/NaN、坏 sidecar、坏 manifest、mirror 目录损坏、ETag 中途
   变化、NoRange/Slow 服务器、offline、重复 id、超大逻辑数据集（逻辑计数）。
2. F-OPS-4 窄修复裁决（按 OWNERSHIP.md 规则执行或保持 OUT_OF_SCOPE）。
3. 退出：边缘测试并入各 fabric 测试文件并全绿。

## Phase 7 — 独立 review（26M）
1. subagent #1：架构 + 科学/语义正确性（overlap policy、provenance、时间语义、
   身份 fail-closed、credential 不落盘）。
2. subagent #2：性能/并发/生命周期/测试可信度（O(page) 契约、取消、RAII、
   断言密度）。
3. P0/P1 全修，高价值 P2 修，accepted debt 记 REVIEW_LOG.md。

## Phase 8 — Rebase + 最终验证 + PR（14M）
1. `git fetch origin && git rebase origin/master`；冲突区域重跑相关测试。
2. 最终 HEAD 全量 fabric 测试 + io 家族回归 + 存在性断言 + `git diff --check` +
   冲突标记扫描。
3. PR_BODY.md 定稿 → runbook 步骤 7/8 → PR 创建，不 merge。

## 里程碑验收总闸
见 GOAL.md Completion gate 1–13。
