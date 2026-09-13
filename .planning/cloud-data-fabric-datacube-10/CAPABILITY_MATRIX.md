# CAPABILITY MATRIX — cloud-data-fabric-datacube-10

规则：每个计划能力必须先证明 master 缺失（实现 / 生产调用方 / 表面 / 测试），才建。
"已存在"声明以读 master 代码为准，不以前代文档为准。

| 能力（10.0） | master 状态 @ 7d78059d1a | 缺口 → 10.0 动作 |
| --- | --- | --- |
| s3:// 等对象存储 URI 归类 | 已存在（ResourceUri VsiRemote，`resource_uri.h:40`） | 保持复用 |
| s3 credential 注入 seam（open 时注入、不持久化） | 缺失（remote/ 无 object store） | 新 `fabric/object_store`：profile scheme → VSI 拼写 + open 时 config 注入 + 注销 |
| signed/public asset、range reads、validator/etag | validator/etag 已有（remote_source_validator）；range 已有（range_cache） | 复用；object store 只补 credential/映射 seam |
| 对象存储 offline policy | offline_gate 已拒绝全部 /vsi* 网络 | 复用；object store 层给 typed 拒绝语义 |
| 本地 STAC 文件集查询 | 单文件 parseFromFile 有；无集合查询 | 新 `fabric/catalog_service` 本地后端（crawl + asset_query 复用） |
| 远程 STAC API 查询 | StacClient 已有（search/pagination/searchAll/客户端过滤） | 复用；service 层补 platform/sensor/assetRole 统一过滤与 cancel |
| 统一 catalog service 接口（local+remote+records） | 缺失 | 新 `fabric/catalog_service` 三后端 + 统一 CatalogQuery |
| catalog 查询 bounded responses | asset_query 有 hardCap/page；StacClient 有 maxItems | 复用；service 层两处都带上界 |
| 资产索引（窗口→相交资产，sublinear） | 缺失（asset_query 只有线性 matches） | 新 `fabric/virtual_cube` 内网格桶索引 |
| 虚拟镶嵌 on-demand 窗口 | 缺失 | 新 `fabric/virtual_cube`：窗口→源资产窗口映射 + 读取 |
| 确定性 overlap policy | 缺失 | 排序键 (instant, id, input order) + first-wins；策略进计划 JSON |
| quality/mask-aware 选择 | cloudCover 客户端过滤已有（StacClient） | 复用 + virtual_cube 选择策略（cloudCover 上限、自定义 rank 键） |
| 逐窗口 provenance（像素→源资产） | 缺失 | virtual_cube 返回 tile→(asset, source window) 记录 |
| cube 命名维/切片/chunk 计划 | multidim_cube 描述符单文件已有 blockShape | 新 `fabric/chunk_plan`：虚拟立方体（资产集合）+ 既有描述符两种源的 chunk 计划；百万 chunk 计数不物化 |
| query planner（intent→bounded plan） | 缺失 | 新 `fabric/query_planner`：stages + cost hints + cancel + 预算 |
| cost hints（bytes/scenes/chunks/memory/remote calls） | hints 有单数据集 bytes/chunk shape | planner 聚合：scenes/chunks/estMemory/remoteCalls/cacheHit 估计 |
| plan 可被 agent 检视 | 缺失 | plan toJson() 稳定键；io:cube_plan 算子暴露 |
| bounded prefetch | 缺失 | 新 `fabric/prefetch`：chunk 计划→range cache 预热，字节预算+取消 |
| offline mirror（显式物化+离线重放） | 缺失（range_cache.h:23-24 明确否认自身是 mirror） | 新 `fabric/mirror`：计划→mirror 目录（atomic publish）；读取解析 mirror 命中 |
| 损坏恢复 | 磁盘层校验和已有（range_cache_disk） | 保持 + prefetch/mirror 路径损坏证据测试 |
| process-safe 缓存语义 | 缺失（进程内锁） | 本 track 记录锁契约（单进程假设显式化），不实现跨进程锁（记 accepted debt） |
| 100k catalog 规模证据 | scale9 有 100k-asset catalog bench（asset_query 线性引擎） | planner 侧新证据：O(page/selected) 内存、百万 chunk 计数 |
| 算子面 | io:* 10 个既有 | additive：io:catalog_search / io:cube_plan / io:cube_window / io:cache_prefetch |
| CLI 面 | data stac/identity/cache 已有 | additive：data catalog search / data cube plan|window / data cache prefetch |

## 重复实现禁令（per /goal invariants）

* 不建第二个 STAC 解析器（复用 stac_mapper）；
* 不建第二个内存查询引擎（复用 asset_query）；
* 不建第二个 cache（fabric/prefetch、fabric/mirror 驱动 RemoteRangeCache 与 atomic_fs）；
* 不建第二个读引擎（virtual_cube 走 RasterReader）；
* 不建第二个调度器（planner 只产出 plan + 执行预算，不占 TaskCenter 职责）；
* 不发明 CF 元数据（轴语义复用 multidim_cube 契约）。
