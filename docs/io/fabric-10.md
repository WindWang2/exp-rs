# Cloud-Native Data Fabric / Data Cube 10.0

`src/geospatial/fabric/` 把 5–9 代建成的离散 I/O 原语（URI、HTTP、range cache、STAC、
multidim、identity、hints）编排成一个可查询、可虚拟化、可分块、可缓存、按需执行的数据
fabric。本模块不做第二套传输/缓存/解析/查询/调度——每一层都复用既有权威（见下表）。

## 模块与契约

| 模块 | 一句话契约 | 复用的权威 |
| --- | --- | --- |
| `object_store` | `s3://`/`gs://`/`az://` → VSI 拼写；credential 以 RAII 窗口注入、析构精确恢复并清 GDAL 句柄缓存；离线 typed 拒绝 | CPL 传输（ADR 0139）、offline_gate、RemoteRangeCache、ResourceUri redaction |
| `catalog_service` | 本地 STAC 树（VSI 遍历 + 链接跟随）/ 远程 STAC API / 内存 records 三后端、同一 `CatalogQuery` 词汇、有界分页、取消、`unresolvable` 诚实计数 | StacClient、asset_query、time_normalization |
| `virtual_cube` | 多场景资产 → 单一逻辑网格：有界探测网格协商、确定性 FirstWins overlap（声明 NoData 让位）、按需窗口读取 + 逐资产 provenance；同 CRS 约束（跨 CRS typed 拒绝，不做 warp） | RasterReader、asset_identity |
| `chunk_plan` | 命名维（time/y/x/band）chunk 计划：u64 总数即时、`materializeChunks(begin,max)` 有界枚举——百万级 chunk 不物化 | multidim_cube（描述符源） |
| `query_planner` | intent → 五阶段计划（catalog_query/asset_selection/grid_planning/chunk_planning/identity_cache）+ cost hints + 预算/取消；流式 Top-K 选择，内存 O(page + selected) | CatalogService、asset_identity |
| `prefetch` | chunk 计划 → `/vsirangecache/` 预热；字节预算按 cache telemetry 实测；mirror 命中跳过 | RemoteRangeCache |
| `mirror` | 计划 → 离线镜像目录（token 键控、fail-closed：不可证身份不镜像）；单写者锁；损坏清单降级跳过 | asset_identity、atomic_fs、sha256 |

## 关键不变量

1. **凭据永不残留**：`ScopedObjectStoreCredentials` 析构恢复先前的 config 键并
   `VSICurlClearCache()`——后续窗口不得复用上一窗口的签名上下文。
2. **O(page + selected)**：`planFabric` 按值接管 intent 并 move 消费 records；计划只持有
   选中场景。100k 记录计划的增量峰值 RSS < 2 MiB（`test_io_fabric_scale` 实测）。
3. **确定性**：选择序 = (云量升序、未声明最后) → (时间降序) → (id) → (输入序)；chunk
   枚举是固定全序；同一 spec + 资产 + 顺序 ⇒ 逐字节相同窗口。
4. **fail-closed 身份**：无 token 资产永不镜像、永不计为 cacheable。
5. **诚实账目**：`clientFilteredOut`（查询过滤）、`unresolvable`（href 不可解析）、
   `truncatedByCap`（爬取上限）分账，绝不混算。
6. **一个解析器**：`fabricIntentFromJson` 是 JSON → intent 的唯一入口（算子与 CLI 共用）。

## 表面

* **C++**：`sicnu/geo/fabric/*.h`。
* **算子**：`io:catalog_search`、`io:cube_plan`、`io:cube_window`、`io:cache_prefetch`。
* **CLI**：`data catalog search <root> [--cloud-max N] [--limit N] [--max-items N] [--platform P]`、
  `data cube plan|window <spec.json> [-o out.tif]`（spec = intent JSON）。

## 边界（本版本明确的限制）

* 跨 CRS 网格 = typed `Unsupported`（建议上游 `io:warp`/`io:reproject`）——fabric 不重写 warp。
* multidim 描述符的 chunk 计划不支持 `CubeSlice` 切片（typed 拒绝）——EO cube 先行。
* GCS 仅匿名公共桶；Azure 自定义端点未接线——均 typed `Unsupported`。
* 缓存/镜像为单进程语义（多进程共享目录 = 另立 track）。
* 真实云端点未在本机验证（loopback S3 兼容端点 + GDAL 真实 /vsis3/ 栈全链路已验证；
  公有云访问标记 not-executed）。

## 测试与证据

六个套件（全绿，330 断言）：`test_io_fabric_object_store`（75，含真实 /vsis3/ loopback
集成）、`test_io_fabric_catalog`（56）、`test_io_fabric_cube`（70）、`test_io_fabric_plan`
（57）、`test_io_fabric_scale`（32，含 RSS 实测内存契约）、`test_io_fabric_operators`（40）。
决策记录：`.planning/cloud-data-fabric-datacube-10/DECISIONS.md`（D-1001..D-1017）。
