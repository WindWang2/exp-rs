# ARCHITECTURE — cloud-data-fabric-datacube-10

## 位置与依赖方向

```
src/geospatial/fabric/           （10.0 新模块；Qt-free，只依赖 sicnu_geospatial 既有层）
  object_store.h/.cpp    —— 对象存储 profile seam（s3://… → /vsis3/…；credential open 时注入）
  catalog_service.h/.cpp —— 统一目录服务（LocalStac | RemoteStac | Records 三后端）
  virtual_cube.h/.cpp    —— 虚拟镶嵌/时间立方体（资产集合 → 按需窗口读取 + provenance）
  chunk_plan.h/.cpp      —— 命名维 chunk 计划（time/y/x/band；计数不物化）
  query_planner.h/.cpp   —— intent → 有界可执行计划（stages + cost hints + cancel）
  prefetch.h/.cpp        —— chunk 计划 → range cache 预热（字节预算 + 取消）
  mirror.h/.cpp          —— 计划/资产集 → 离线镜像目录物化 + 命中解析
```

依赖方向（单向向下）：fabric → {stac, catalog, multidim, hints, identity, raster,
remote, util, formats}。既有层零反向依赖（除 CMake 注册与 remote 窄挂钩点）。

## 核心契约草案（Phase 1 定稿）

### ObjectStoreProfile（fabric/object_store）
* `struct ObjectStoreProfile { scheme, vsiPrefix, endpointStyle, regionRequirement }`；
  内置表：`s3://` → `/vsis3/`、`gs://` → `/vsigs/`、`az://` → `/vsiaz/`（声明即注册，
  可 lookup/extend）。
* `resolveToObjectStore( uri ) → ResourceUri + profile`：非对象存储拼写 typed 拒绝
  （InvalidArgument）；`ResourceKind::VsiRemote` 既有拼写直通。
* credential 注入：`ScopedObjectStoreCredentials`（RAII，open 期间 set、析构 unset 的
  GDAL config option 对；**文档化串行 open 假设**：注入窗口内不开新线程 open 其他
  bucket）。凭据仅从 caller 显式传入（结构体字段），绝不写入 identity/display/日志/
  JSON 报告（display 走 ResourceUri::display() 既有 redaction）。
* offline：open 前查 `offline::enabled()` → typed `GeoError(NetworkError)` 引用
  offline 拒绝文案；anonymous/public 读取走 `/vsicurl/` 或无凭据 `/vsis3/`。
* validator/etag：open 经 `/vsirangecache/` 拼写时复用 RemoteSourceValidator 全契约。

### CatalogService（fabric/catalog_service）
* `struct CatalogQuery { bbox, temporalStartUtc/EndUtc, collections, ids, cloudCoverMax,
  platform, sensorInstruments, assetRole, mediaTypeSubstring, limit, maxItems }`；
  `validate()` 前置形状校验（对齐 StacSearchQuery::validate 风格）。
* 后端枚举 `CatalogBackend { LocalStacFiles, RemoteStacApi, InMemoryRecords }`；
  `openCatalogService( uriOrRecords, options )`：ResourceUri 归类分派（LocalDirectory/
  LocalFile → 本地；RemoteHttp → 远程；内存 records → 引擎）。
* `CatalogPage searchPage( query, continuation )` + `searchAll( bounded )`；远程翻页
  复用 StacPage 继续描述符；本地以 (offset) 继续。每页 `records` 为
  `std::vector<AssetRecord>`（复用 assetRecordFromStacItem）。
* 过滤语义：server 能推的推（bbox/datetime/collections/ids/limit → STAC 参数），
  platform/sensor/role/cloudCover 在 service 层统一 client 侧过滤（与 StacClient
  静态过滤同一真值函数），计数诚实（`carriedByClientFilter` 统计）。
* cancel：`CancelToken`（std::atomic<bool> 值类型）在页间检查 → `GeoError(Cancelled)`。
* offline：RemoteStacApi 在 offline 时 typed 拒绝；LocalStacFiles/InMemoryRecords 不受
  影响（这是"离线本地重放"的第一半，第二半是 mirror）。
* 诊断：所有错误/日志 URL 走 display() redaction；query cache 复用 StacClient 内建
  （opt-in）。

### VirtualCube（fabric/virtual_cube）
* 构建：`VirtualCubeSpec { assets: vector<AssetRecord>（已解析可读路径）, grid:
  optional<TargetGrid>, overlap policy, quality policy }`；`TargetGrid { crs, scaleX/Y,
  minX…maxY }`；未显式给 grid → 从资产确定性派生（最高分辨率优先；平局按 (epsg, scale,
  id) 字典序），规则进 DECISIONS。
* 资产打开是 lazy 的：构建只做 bbox/时间索引（网格桶 AABB 索引：固定 cell 尺寸
  派生自资产 bbox 中位数 → 窗口查询 O(相交 cell)）；索引内存 O(资产数) 但构建输入
  已是 planner 选出的 ≤ sceneBudget 个资产（O(selected) 契约在 planner 层）。
* `readWindow( bands, window, options ) → WindowReadResult`：像素 buffer +
  `provenance`（每个覆盖 tile 的 (assetId, sourceWindow, transform)）+ per-asset 错误
  记录；overlap first-wins（按 selection order）；NoData 填充未覆盖区。
* 读路径：资产路径（远程）→ `RemoteRangeCache::cachedPath()`（安装时）/ 直读
  RasterReader；重投影仅在 grid CRS ≠ 资产 CRS 时经 readWindowResampled + 显式
  warp 契约（Phase 3 定：首版要求资产与 grid 同 CRS，否则 typed Unsupported——
  记 DECISIONS，避免重写 gdalwarp）。
* 波段角色：资产来自 AssetRecord.metadata 的 role 键 / canonical bandRole；band 选择
  按角色名解析。

### ChunkPlan（fabric/chunk_plan）
* `CubeChunkPlan { dims: [time, y, x, band]（命名维 + 尺寸）, chunkShape, chunkCountTotal
  （u64，支持百万级），chunks(): 有界物化器（按 index range 枚举，一次最多 plan.
  maxMaterializedChunks 个）}`。
* `planChunks( cube, shape, slicing )`：slicing 含 time range（UTC instant）、bbox、
  band roles → 逻辑 chunk 集合；每个 chunk 记录 time instant + y/x extent + band 集 +
  est bytes（从 hints/资产 bytes 派生）。
* 虚拟立方体源：chunk → 候选资产映射（经虚拟立方体索引）；multidim 源：chunk →
  变量 block。

### FabricPlan / QueryPlanner（fabric/query_planner）
* `FabricIntent { catalog uri/records, CatalogQuery, sceneBudget, chunkShape, region
  (bbox), timeRange, bandRoles, executionBudgetBytes }`。
* `FabricPlan`：stages 数组（每 stage：名字、输入计数、输出计数、状态枚举 planned/
  skipped/estimated），cost hints { scenes, chunks, estimatedBytes, estimatedMemory
  Bytes, estimatedRemoteCalls, cacheableTokens }；`toJson()` 稳定键。
* `planFabric( intent, options, cancel )`：catalog_query 阶段流式分页消费（页边界
  即丢页，只保留选中资产 ≤ sceneBudget —— O(page + selected) 内存契约）；
  asset_selection 按 quality policy 排序取前 K；grid/time planning；chunk_requests
  （ChunkPlan）；cache 阶段产出每资产 identity token（复用 asset_identity，provable
  才 token，fail-closed）。
* `executeFabric( plan, sinks, cancel )`：按计划读窗口/chunk（写输出经既有原子写
  writer；或聚合统计 sink）；预算超限 → ResourceExhausted；cancel → Cancelled；
  执行报告（成功/失败资产、bytes fetched、cache hits）。

### Prefetch / Mirror（fabric/prefetch, fabric/mirror）
* `prefetchChunks( plan, budget, cancel )`：对 chunk 计划逐项经 `/vsirangecache/` 路径
  触发块读（RasterReader 小窗读），全局字节预算 + cancel；报告 per-chunk 状态；
  不绕过 cache 直接拉流。
* `mirrorPlan( plan, mirrorDir, budget, cancel )`：把 chunk 计划命中的源资产窗口物化为
  mirror 目录下的 COG-shaped 块文件（复用 atomic_fs publish + 简单 manifest JSON；
  manifest 键 = identity token（provable 才入 mirror，同磁盘层 fail-closed 规则））。
* `resolveMirrorHit( assetPath, token ) → local path | ""`：读取解析时优先 mirror；
  offline 时 mirror miss → typed 拒绝（引用 offline 文案）。
* 锁契约：mirror manifest 文件为单写者（同目录锁文件 O_EXCL），读取无锁（原子
  publish 保证）；跨进程共享 mirror 目录的并发写按"先 publish 后可见"容忍。

## 与既有权威的收敛点（防第三套）

| 需求 | 复用 | 不新建 |
| --- | --- | --- |
| HTTP | remote/http_fetch (CPL, ADR 0139) | 不引入 curl/SDK |
| range 缓存 | remote/range_cache（+disk） | 不建第二缓存 |
| 身份 | identity/asset_identity（li1/sd1/ri1 fail-closed） | 不建第二 token |
| STAC 解析 | stac/stac_mapper | 不建第二解析器 |
| STAC 传输 | stac/stac_client | 不建第二 API 客户端 |
| 内存查询 | catalog/asset_query | 不建第二查询引擎 |
| 时间归一 | util/time_normalization | 不建第二 UTC 逻辑 |
| URI/安全显示 | util/resource_uri | 不建第二 redaction |
| 本地窗口读 | raster/raster_reader | 不建第二 reader |
| 原子落盘 | util/atomic_fs | 不建第二 publish |

## 错误分类（复用 GeoError）

InvalidArgument（形状/参数）· Unsupported（同 CRS 约束、未注册 scheme、未建驱动）·
UnsupportedFormat · InvalidMetadata（坏 sidecar/manifest）· NetworkError（远程失败，
offline 拒绝也归此并引用 offline 文案）· Timeout · ResourceExhausted（预算/上限）·
Cancelled。
