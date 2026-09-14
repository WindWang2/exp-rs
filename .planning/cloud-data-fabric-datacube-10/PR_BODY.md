# PR BODY — cloud-data-fabric-datacube-10（Phase 9 定稿）

## Baseline

origin/master @ `7d78059d1a6d316d606656759a506d17bc5e3b55`；worktree
`../exp-rs-cloud-data-fabric-datacube-10`；分支 `zcode/cloud-data-fabric-datacube-10`。

## Architecture

新模块 `src/geospatial/fabric/`（Qt-free，仅依赖 sicnu_geospatial 既有层）把 5–9 代的
离散 I/O 原语编排为数据 fabric，零反向依赖、零第二实现（传输=CPL/ADR 0139、缓存=
RemoteRangeCache、身份=asset_identity、STAC=stac_client/mapper、内存查询=asset_query、
原子落盘=atomic_fs）。七个子模块的契约一句话版见 `docs/io/fabric-10.md`；
决策 D-1001..D-1017 见 `.planning/cloud-data-fabric-datacube-10/DECISIONS.md`。

## Major deliverables

1. **对象存储 seam**（`fabric/object_store`）：s3/gs/az → VSI profile 注册表（无 SDK）；
   credential RAII 窗口（精确恢复 + VSICURL 缓存清理，凭据零残留）；loopback S3 兼容端点
   + GDAL 真实 /vsis3/ 栈的全链路集成验证。
2. **统一 catalog service**（`fabric/catalog_service`）：本地 STAC 树（VSI 遍历+链接跟随、
   环安全）/ 远程 STAC API / 内存 records 三后端一套查询词汇；有界分页、取消、
   clientFilteredOut / unresolvable / truncatedByCap 诚实分账。
3. **虚拟镶嵌/时间立方体**（`fabric/virtual_cube`）：多场景资产→单一逻辑网格（有界探测
   协商、最高分辨率获胜）；确定性 FirstWins overlap（声明 NoData 让位回填）；按需窗口
   读取 + 逐资产 provenance；跨 CRS typed 拒绝（不做隐藏 warp）。
4. **chunk 计划**（`fabric/chunk_plan`）：命名维 time/y/x/band；u64 总数即时（百万级
   chunk 不物化）；时间/空间/波段切片；dtype 字节估算。
5. **query planner**（`fabric/query_planner`）：intent→五阶段可检视计划 + cost hints；
   流式 Top-K 选择 O(page+selected)——100k 记录计划增量 RSS < 2 MiB（实测断言）；
   身份可缓存性有界探针；执行预算（超额=报告跳过而非崩溃）与 typed 取消。
6. **Cache 10.0**（`fabric/prefetch`、`fabric/mirror`）：chunk 计划驱动的 range cache
   预热（telemetry 实测字节、预算有界默认启用、mirror 命中跳过）；token 键控离线镜像
   （fail-closed：不可证身份不镜像；单写者；原子清单；损坏降级跳过）。
7. **表面**：`io:catalog_search` / `io:cube_plan` / `io:cube_window` /
   `io:cache_prefetch` 算子；`data catalog search`、`data cube plan|window` CLI；
   `fabricIntentFromJson` 唯一 JSON 意图解析器（算子与 CLI 共用）。
8. **F-OPS-4 窄修复**（#957 遗留 P1，D-1013）：io:reproject 的 srcCrsOverride 现在真正
   进入 warp（`-s_srs`）——CRS-less 输入不再以目标 CRS 标签输出未变换像素网格。

## Compatibility

全 additive：新模块/新算子/新 CLI 子命令/新测试；既有 API 无签名变更。两处共享文件的
最小侵入：`WarpOptions` 增可选 `sourceCrsOverride` 字段（默认空 = 原行为）、
`CatalogPage/SearchAllResult` 增计数字段（fabric 自有类型）。io 家族 7 套件 +
test_io_operators 回归全绿（574 断言）。

## Tests

最终 HEAD（1b8bfa7b2e）全量：**14/14 套件全绿，918 断言**——fabric 六套件 344
（object_store 74 / catalog 56 / cube 70 / plan 72 / scale 32 / operators 40）+
io 家族回归 574（uri 96 / stac 22 / stac_client 136 / range_cache 123 / remote_range 25 /
catalog_query 50 / hints 27 / operators 95 含 F-OPS-4 回归）。逐套件命令与输出见
`.planning/cloud-data-fabric-datacube-10/EVIDENCE.md`。

## Performance / Resource

* 100k 记录计划：峰值 RSS 增量 < 2 MiB（ru_maxrss 实测断言）——O(page + selected)。
* 1,048,576 chunk 计划：计数即时、窗口枚举有界、plan JSON < 256 KB。
* 4096² 逻辑网格 512² 窗口读：RSS 增量 < 16 MiB（窗口自身 2 MiB）——O(window)。
* 编译全程 -j1/-j2（主机 62G 内存、/tmp inode 压力事件已记录）；无全量无界并行。

## Review findings

两个只读对抗审查（架构+语义 / 性能+并发+测试可信度）共 36 findings + 主 agent 复核
4 项：**P0 = 0；P1 = 5 项全部修复**（planner 本地目录死循环、协商网格重推导、
prefetch/mirror 默认预算无界、telemetry 键名失效、S3 缓存路径组合损坏——后两项由
新增的真字节集成测试暴露）；P2 13 项中 12 修复、1 项记 follow-up（远程资产离线镜像
重放）；P3 16 项中 13 修复、3 项 accepted-debt。逐条处置：REVIEW_LOG.md。

## Known limitations

* 跨 CRS 虚拟网格 typed 拒绝（建议上游 io:warp/io:reproject）——fabric 不重写 warp。
* 多维（multidim）描述符的 chunk 计划不支持切片（typed 拒绝；EO cube 先行）。
* 远程资产的离线镜像重放需无网络 token 解析（本地文件镜像离线重放已验证）。
* S3/gs/az 的块缓存键（range cache 目前原生覆盖 http(s)）；GCS 仅匿名公共桶；
  Azure 自定义端点未接线——均 typed。
* 缓存/镜像单进程语义；真实公有云访问 not-executed（loopback S3 全链路已验证）。

## Follow-ups

* S3/gs/az 对象的 range-cache 键（URL 形状组合）；远程离线镜像重放的无网络 token 解析；
  multidim 描述符切片；执行期 per-chunk 遥测（进程独占假设的消除）。

## Evidence policy

Local evidence only; no online CI dependency.（本地 build/test/bench 证据，未等待、
未触发、未引用任何线上 CI。）
