# DECISIONS — cloud-data-fabric-11

编号规则：D-11xx（接续 fabric 10.0 的 D-10xx 系列）。每条：背景 / 候选 / 取舍 / 影响。

## D-1101 — 跨协议 canonical object token（WP A）

**背景**（审计 `audit/subagent1-gap-audit.md` WP A）：`assetIdentityToken("/vsis3/b/k")`
必然失败——`ResourceUri::remoteUrl()` 对非 http 返回裸 "bucket/key"，validator 抛
InvalidArgument 后被吞成 token=""（unprovable）。同一对象经 `s3://b/k`、`/vsis3/b/k`、
`https://<endpoint>/b/k` 三种拼写无法收敛。

**候选**：
1. 在 validator 内新增 VSI-object 探测分支（改 identity authority 的 http fetch 层）；
2. 在 fabric/object_store 层新增 canonical 化函数：`s3://b/k`、`/vsis3/b/k`、endpoint URL →
   provider 规范形态（canonical object key），identity basis 用该规范形态 + ETag（ETag 获取
   复用既有 validator 对重建 http(s) URL 的探测）。

**取舍**：候选 2。理由：不触碰 8.0 已稳定的 remote identity authority（read-only 兄弟域）；
canonical 化是纯字符串/FS 逻辑（离线可用，与 WP C 离线索引共享）；失败语义保持 fail-closed
（无 ETag ⇒ unprovable ⇒ 不缓存不镜像）。

**影响**：新增 `fabric/object_store` API：`canonicalObjectKey(path) → {provider, bucket, key,
canonical}`；`assetIdentityToken` 的 object-store 路径走 canonical URL 重建探测。etg/version/
generation 字段加入 resolution（VSI StatL 的 ETag 字段已有 GDAL 支持）。

## D-1102 — range cache 的 VSI-object 模式（WP B）

**背景**：`fabricCachedPath` 只包装 http(s)（`object_store.cpp:228-241` 自注 follow-up）；
cache 的 `underlyingUrl` 只剥 /vsicurl/；fallback 构造 "/vsicurl/"+payload 对 VSI payload 无效；
cache key 无凭据维度（跨账号块复用风险）。

**候选**：
1. cache 内部把 /vsis3/ payload 重写为 http(s) URL 直接用现有 http fetcher（签名问题需自己解，
   与 GDAL 签名/凭据逻辑重复，危险）；
2. cache 的取数层新增 VSI-object 分支：对 /vsis3|gs|az/ payload 经 `VSIFOpenL("/vsis3/…")` 读
   ranges（GDAL 处理签名/凭据，凭据窗口 D-1003 序列化保证下安全）；fallback 同样重开原 /vsi*
   拼写而非 /vsicurl/；资源键 = canonical object key（D-1101）+ 凭据上下文指纹
   （endpoint+accessKeyId 的 SHA-256 前缀——非凭据本身，secret 永不入键/日志）。

**取舍**：候选 2。签名/凭据是 GDAL 的活（ADR 0139：CPL is the transport），自建 http 签名违反
仓内既定边界。

**影响**：`RangeCacheConfig` 增加可选凭据上下文 setter（由 ScopedObjectStoreCredentials 窗口
驱动）；memory 层增补逐块 SHA-256 校验（对齐 disk 层既有 trailer 方案）+ 损坏自愈计数遥测；
`fabricCachedPath` 对 object-store 路径按新语义包装。

## D-1103 — mirror index v2 与离线重放（WP C）

**背景**：重放三处网络触点：readWindow 先开原资产再查 mirror（`virtual_cube.cpp:474` vs
`:529`）；token 懒探针联网；无持久 token 库（manifest 虽 token 键但无 path→token 索引）。
10.0 自认债 A-R5。

**候选**：
1. GDAL VSI 结构化缓存 key 化（改 GDAL 层——越界，否决）；
2. manifest v2：新增顶层 `index`：canonicalObjectKey → {token, chunks:{chunkKey → {file,bytes,
   sha256,writtenAt}}}；读取方离线计算 canonicalObjectKey（纯字符串）→ 得 token → 按窗口算
   chunkKey 命中；readWindow 改为 **mirror-first**：mirrorDirectory 非空时先用 build 期捕获的
   index 网格事实（VirtualCubeAssetIndexEntry 已有 resX/resY/bbox）映射源窗口并查 mirror，
   命中则开本地文件，未命中才走原网络路径；
   完整性：size 恒校验 + sha256 按 verify 策略；过期：writtenAt + maxAge（默认不过期，显式声明）。

**取舍**：候选 2。manifest v2 向后兼容读 v1（无 index 节 → 行为同今天）；mirror-first 仅在
命中时短路，未命中的失败语义与今天完全一致（per-asset provenance）。

**影响**：`resolveMirrorArtifact(dir, canonicalKey, chunkKey)` 新 API；`VirtualCubeReadOptions`
增 `offlineReplay`/`maxMirrorAge` 选项；Oracle 1 用 loopback S3 + forced offline + 请求计数
断言零网络。v1 chunk 继续有效（同 manifest 渐进升级）。

## D-1104 — multidim range 切片与执行路径（WP D/E）

**背景**：multidim 只能单 index 切片；forMultidimDescriptor 拒绝 CubeSlice；尾轴 temporal
时静默零计划；无 multidim 执行面。

**候选**：
1. 改 MultidimView 契约支持多自由维（破坏 4.0 起 "恰好两自由维、flatten-to-bands is forbidden"
   的 standing contract，影响面大）；
2. 新增 `MultidimView::readSliceRanges(variable, dimRanges[name→(begin,end)], rowOff, colOff,
   rows, cols, maxCells)`：非空间维支持 [begin,end) range，结果仍要求两尾随自由维；range 内
   多 index 由 fabric 层逐组合物化（每组合一次 readSliceWindow 复用读路径，GDAL 原生 chunk
   语义不变）；`forMultidimDescriptor` 支持 CubeSlice（time 用 descriptor 的 instants 语义、
   空间 slice 在 y/x 尾轴、band/命名维用 range）；修复尾轴 temporal 静默零计划（显式
   Unsupported + 说明）；补 `executeChunks` 的 multidim 分支（经 readSliceRanges）。

**取舍**：候选 2。守住 standing contract（不 flatten），slice 语义在 planner/fabric 层组合，
读路径单一路径原则不变。

**影响**：CubeChunkShape 增 per-name chunk 覆盖（additive 字段）；计划期字节事实改为消费
build 期 probe 捕获（消除 forVirtualCube 计划期网络探针——审计 sharp edge #10）；known-answer
用 GDAL 直读 reference 比对（Oracle 3）。

## D-1105 — mirror/prefetch/CLI 缺陷批次修复（随 WP 一并交付）

审计 sharp edges 中在本 track scope 内的修复：
- #2 manifest 节流死代码 → 节流真实生效（每 32 chunk 一写 + 终写）；
- #3 MirrorReport.chunks 上限（1024 窗口 + outcomesDropped，对齐 prefetch/executeChunks）；
- #4 预算基准统一（统一为声明字节估计 vs 真实字节双列报告，早停按同一基准）；
- #5 mirror key 的 path 分量改为 canonicalObjectKey（D-1101，v1 键继续可读，新写入用 v2）；
- #12 读预算走 MirrorOptions（默认保持 256MiB 语义）；
- #13 resolveMirrorHit 增 manifest 内存缓存（mirrorChunksImpl 已缓存的路径对齐）；
- #1 CLI `data cube window` 对齐算子侧 A-R3/A-R6（writer NoData/dtype + setCrs 守护）；
- #14 落地 `data cache prefetch`（D-1012 兑现）与 `data mirror materialize|stats`、
  multidim descriptor 的 cube 面接入（fabricIntentFromJson 单解析器词汇扩展）；
- io:cache_prefetch 增加可选 cache install/config 参数。
不在本 track scope：#15 中 query_planner 双类型 asUInt64（改 P3 记录）、B-R14/B-R15（10.0
accepted，prefetch 重写时顺带评估）。

## D-1106 — 构建环境（本机事实）

cmake/ninja/cl.exe 不在 Git Bash PATH。采用 VS2022 `vcvars64.bat` + `C:/Qt/Tools/Ninja` +
`CMAKE_PREFIX_PATH=C:/deps/Qt/6.8.0/msvc2022_64;C:/deps/qca-install;C:/deps/kc-install` +
vcpkg 工具链 `C:/deps/vcpkg/scripts/buildsystems/vcpkg.cmake`（manifest 63 包已从本机二进制
缓存 13s 还原；FetchContent Catch2 用 `C:/deps/catch2-src`）。驱动脚本
`build-dev/track-cmd.cmd`（configure/build/test 三态，-j2/-j1 硬约束内置）。
