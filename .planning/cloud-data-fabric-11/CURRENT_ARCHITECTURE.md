# CURRENT_ARCHITECTURE — cloud-data-fabric-11（基线 = master@a5b11b7f）

## 模块与 authority 图（本 track 域内）

```
CLI `data ...`(src/cli/cli_commands.cpp commandData 区段 ~L1850-2135)
  └─ FabricIntent/fabricIntentFromJson (fabric/query_planner)  ← 唯一 JSON→intent 解析器
       └─ planFabric → FabricPlan{stages, cost, selected, grid, chunkPlan}
            ├─ CatalogService (fabric/catalog_service)          ← 目录查询 authority
            │    ├─ local STAC tree / remote STAC API / in-memory records
            ├─ VirtualCube (fabric/virtual_cube)                ← EO cube authority
            │    ├─ AssetRecord (geospatial/catalog/asset_query)
            │    ├─ RasterReader (geospatial/raster)            ← 像素读 authority
            │    ├─ assetIdentityToken (geospatial/identity/asset_identity) ← 身份 authority
            │    │    ├─ local: li1:v1:<hex> (path+size+mtime+inode+hash-prefix)
            │    │    └─ remote: ri1:v1 ETag fail-closed（credential-shaped query 剥离）
            │    └─ mirror 偏好读（readWindow: token+sourceWindow+band1 → fabricChunkMirrorKey）
            ├─ CubeChunkPlan (fabric/chunk_plan)                ← 枚举 authority（D-1008）
            │    ├─ forVirtualCube (time/y/x/band；slice 全支持)
            │    └─ forMultidimDescriptor（非默认 slice = typed refusal，chunk_plan.h:126）
            │         └─ MultidimCubeDescriptor (multidim/multidim_cube) ← MDArray 描述 authority
            │              └─ MultidimView (multidim/multidim_view)      ← MDArray 读 authority
            │                   readSlice/readSliceWindow（两自由维；maxCells 预算）
            ├─ executeWindow / executeChunks（query_planner）
            ├─ prefetchChunks (fabric/prefetch)                 ← plan 驱动 cache 预热
            └─ mirrorChunks (fabric/mirror)                     ← chunk 物化（token 键）
                 manifest.json + chunks/<sha16>.tif（原子发布；D-1010 fail-closed；D-1014 单写者锁）

RemoteRangeCache (geospatial/remote/range_cache)               ← /vsirangecache/ VSI handler
  ├─ LRU byte budget + coalescing + validator 策略(RevalidateOnOpen/ValidateOnce/TrustForever)
  ├─ 9.0 disk block 层（content-identity keyed）
  └─ fabric/object_store: resolveObjectStore（s3/s3a/s3c/gs/az → /vsi*）、
       ScopedObjectStoreCredentials（RAII 精确恢复 + VSICURL 句柄缓存擦除；D-1003 进程序列化）、
       fabricCachedPath（fetchable → /vsirangecache/ 前缀映射）、fabricOfflineRefusal
```

## 并行兄弟模块（read-only，本 track 只做 additive 消费）

- `src/geospatial/util/resource_uri`：URI 分类 + display 脱敏（credential redaction）。
- `src/geospatial/util/sha256`、`atomic_fs`：哈希与原子发布原语。
- `src/geospatial/hints/data_locality`：locality 提示（WP F 的消费对象）。
- `src/geospatial/remote/remote_source_validator`（RemoteSourceValidator/remoteIdentityToken）：
  ETag 采集与 revalidate。
- `tests/support/http_range_server`、`http_stac_server`：loopback HTTP 测试设施。

## 已证实的缺口（本 track 的深挖方向；详细 file:line 见 subagent 审计报告存档）

1. **WP A**：assetIdentityToken 的 remote 路径以 URL 字符串为 basis — `s3://b/k` vs `/vsis3/b/k` vs
   同一对象不同协议拼写是否收敛到同一 token 未证明；ETag 之外 version/generation（GCS generation、
   Azure etag）未显式建模。
2. **WP B**：range cache 对 /vsis3/ 等对象路径：fallback 语义（degrade → /vsicurl/ 不带 S3 签名）、
   cache key 与 credential context 的分离、损坏自愈（block 校验面）需逐项验证。
3. **WP C**：mirror replay 依赖 VirtualCube::build 的 metadata probe 与 readWindow 内
   assetIdentityToken 探测（virtual_cube.cpp:437,544）→ **断网时无法完成 replay**：
   缺 token→artifact 的离线索引（token 直接命中，免身份探测）。
4. **WP D/E**：forMultidimDescriptor 拒绝 slice（chunk_plan.h:126 "slice narrowing for multidim
   stores is a follow-up"）；multidim 计划缺 EO 侧的 assetIdHint 等价物与字节事实核验。
5. **WP F**：prefetch 是 plan 顺序驱动，无访问模式（窗口序列）驱动的 locality 排序/协调。
6. **WP G**：CLI `data` 缺 mirror/prefetch 子命令；`data cube` 不接受 multidim descriptor intent。
7. **WP H**：loopback 设施已有（http_range_server/http_stac_server + /vsis3/ 真实 loopback 测试），
   需扩展 forced-offline + corruption 注入 + token 索引规模。
