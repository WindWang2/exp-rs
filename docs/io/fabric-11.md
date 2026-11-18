# Cloud Data Fabric 11.0 — object identity, object-path caching, offline replay, multidim

基线：Data Fabric 10.0（`docs/io/fabric-10.md`）。本节记录 11.0 在同一批 authority 之上的增量契约。
Local evidence only — 全部证据为本地可复现命令（`.planning/cloud-data-fabric-11/TEST_MATRIX.md`）。

## 统一对象 identity（WP A，DECISIONS D-1101）

对象存储拼写第一次拥有**拼写无关**的身份：

* `canonicalObjectKey(resource)` — `s3://b/k`、`s3a://b/k`、`s3c://b/k`、`/vsis3/b/k` 全部收敛为
  `s3://b/k`（gs/az 同理）。纯字符串；userinfo/query/fragment 形状 `valid=false`。
* `fabricAssetIdentity(resource)` — fabric 层唯一身份入口：对象拼写走 canonical key 身份
  （强 ETag ⇒ `ri1:v1:` token；弱/`null`/无 ⇒ "" fail-closed）；其余拼写原样委托
  `assetIdentityToken`。**同一对象经任何对象拼写收敛到同一 token**；http(s) URL 身份保持
  8.0 basis（无端点知识时 URL 与 bucket/key 拼写不能证明字节等同——诚实边界）。
* ETag/version 捕获：`probeObjectStoreIdentity` 经 VSI 栈 HEAD（GDAL 签名，D-1003 序列化）
  捕获 `ETag` 与 `Content-Length`；GCS generation / Azure etag 字段化为 follow-up
  （当前通道已就绪，字段未单列）。

凭据从不进入 token、指纹、缓存键或日志（既有 8.0 规则，全部新增路径再验证）。

## 对象路径的 range cache（WP B，DECISIONS D-1102）

10.0 的已知缺口——`/vsis3|gs|az/` 不被 `/vsirangecache/` 包装——已关闭：

* `fabricCachedPath` 现在包装对象路径；scheme 拼写先经 profile 表归一（s3a/s3c → `/vsis3/`），
  一个对象每个主体只有一个缓存资源。
* 取数经 VSI 栈（GDAL 签名，跟随活动凭据窗口）；**失败 fallback 重开同一 `/vsi*` 拼写**，
  不再构造无签名 `/vsicurl/` 组合（M-R2 的对偶缺陷随构造性消失）。
* **凭据上下文分离**：`ScopedObjectStoreCredentials` 窗口把非秘密形状指纹
  （`objectStoreCredentialContext`：prefix+endpoint+keyId+token 存在性+anonymous 的 16 hex
  SHA-256 前缀）注入缓存键。不同账号/匿名窗口的块永不互相服务；secret 本身永不进入指纹。

## 离线镜像重放（WP C，DECISIONS D-1103）

10.0 自认债 A-R5（"远程离线重放需无网络 token 机制"）已关闭：

* **manifest v2**：新增顶层 `index`：资产凭据无关键（`fabricMirrorIndexKey`：对象 canonical
  key / 去凭据 identity URL / 本地 canonical path）→ `{token, assetId, grid{width,height,
  geotransform,epsg}, writtenUtc}`。v1 的 token→chunk 条目原样保留；v1 清单可继续读取。
* **重放零网络**：`VirtualCube::build` 接受 `mirrorDirectory`——离线索引里的 token 与网格事实
  直接进条目（`fromMirrorIndex=true`，不花 probe 预算）；`readWindow` 改为 **mirror-first**：
  命中即用本地 chunk，远程资产根本不打开。Oracle 测试断言全程 `requestCount` 不变。
* **完整性/过期**：命中做 size 校验（chunk bytes 与 manifest 一致）；manifest 记录 sha256 与
  `writtenUtc`；`VirtualCubeReadOptions.maxMirrorAgeSeconds`（0=不过期，10.0 语义）使过期
  命中变成 typed miss。
* 缺口（未宣称）：整对象镜像（非 chunk 级）仍不存在；重放粒度 = 镜像粒度。

## GDAL multidim 与 planner（WP D/E，DECISIONS D-1104）

* `CubeSlice.dimensionRanges`：任意命名维的 `[begin,end)` 切片（含 "band"）；
  `CubeChunkShape.perDimension`：命名维 chunk 尺寸。
* `forMultidimDescriptor` 真实切片：time 用 descriptor 已解析 instants（**有界捕获轴拒绝
  切片**——缺失不是证据）；bbox 空间切片需要声明 geotransform；bandRoles/bandIndices 仍为
  EO-only（多维用 dimensionRanges，typed 提示）。
* **尾轴 temporal 不再静默零计划**：typed Unsupported（"no y/x pair"）。
* `FabricIntent.multidim{path,variable}`：计划跳过 catalog/selection/grid 阶段
  （`store_open` + `chunk_planning`）；`executeChunks` 的多维分支按块枚举、按非空间维逐
  索引组合 `readSliceWindow`（唯一读路径，无第二切片机）。
* million+ 逻辑 chunk 的 u64 计数与有界枚举契约（D-1008）扩展到多维侧——计划可以命名
  十亿级 chunk 而不物化任何一个。

## CLI / operator 面（WP G）

* 新增：`data mirror materialize <spec.json> -o <dir> [--max-bytes N]`、
  `data mirror stats <dir>`、`data cache prefetch <spec.json> [--max-bytes N]`（D-1012 兑现）。
* `data cube` 的 spec 词汇扩展（唯一解析器 `fabricIntentFromJson`）：`multidim{path,variable}`、
  `chunkShape.perDimension`、`slice.dimensionRanges`。
* CLI `data cube window` 与 `io:cube_window` 算子输出对齐（NoData/Float64 band、无 CRS 派生
  网格不抛——10.0 REVIEW_LOG A-R3/A-R6 的 CLI 侧回归修复）。

## 边界（诚实清单）

* 对象身份在 http(s) URL 与 bucket/key 拼写间**不**跨通道合并（D-1101 的论证）。
* mirror 仅物化 EO 计划 chunk（band 1）；整对象拉取、 multidim 镜像 = follow-up。
* 缓存/镜像仍是单进程语义（D-1014 不变）；多进程共享目录另立 track。
* 公有云真实端点（AWS/GCS/Azure）访问保持 10.0 的 not-executed 状态；loopback S3 全链路
  是本仓的云端证据标准。

## 分页有界与诚实记账 / VRT 代际保留（R33-T03 增量）

### prefetch / mirror 分页与记账（`src/geospatial/fabric/prefetch.cpp`、`mirror.cpp`）

* `chunkWindow > 4096` 不再静默漏块：walker 的步进等于它实际物化的有界批量
  （`min(chunkWindow, 4096)`），此前步进按整个 window 而物化最多 4096 个请求，
  `(4096, chunkWindow)` 区间的 chunk 既不被预热/镜像也不出现在报告里。
* 取消记账守恒：取消首次出现在 window 边界时同样计入 `skippedCancel`（此前静默），
  且内层余量与外层边界只报告一次（`walkCancelled` 标志，不会双计）。
* `outcomesDropped` 真正写回报告（此前 prefetch 侧恒为 0；mirror/query-planner 侧
  本来就已赋值）。
* 默认预算乘法饱和：`estimatedBytes × total` 溢出时取 `UINT64_MAX`，不回绕成
  小预算（跳过全部）或看似无界的值。

### 数据集 patch 准入（`src/dataset/patch_generator.cpp`、`sample.{h,cpp}`）

* NoData 比例阈值拒非有限值：NaN 曾通过 `(0,1)` 比较（两个比较都为 false），随后
  让每个 patch 的 `validFraction >= NaN` 静默为假——现在 typed 拒绝。
* 网格 patch 计数使用 checked ceil-div 与乘积范围检查：`(w + s - 1) / s` 与
  `tilesX * tilesY` 的 qint64 溢出在任何枚举/分配前 typed 拒绝。
* `PixelWindow` 的半开端点在 `contains`/`intersects`/`intersected` 中经
  `saturatingEnd()` 计算，footprint 边沿在 double 空间计算——极端尺寸不再有
  有符号溢出 UB；可表示输入的结果不变。

### VRT 代际保留（`src/data/providers/virtual_raster_generation_lease.{h,cpp}`）

* 受管理 reader 在 `ProcessingAssetResolver::resolve`（VirtualRaster 输入）取得
  `VirtualRasterGenerationLease`：发布后 sweep 不再删除当前发布引用的 warp 中间
  代际的文件，最后一个 lease 释放时才做延迟删除。文件级 promise——VRT XML 自身
  仍是"关闭/重开"合同，任意外部 GDAL handle 同样遵循该合同，lease 不声称能恢复
  被外部原地改写的字节。
* sweep 的 live 集合包含每个 live 名字的 `.identity.json` sidecar（provider 的
  live 集本就是每输入两个文件；只从 VRT XML 推导的集合曾把当前代际的 sidecar 删掉，
  使暂存复用失效）。
* 有界：每 VRT 最多保留 4096 个不同代际，超出时 acquire 返回无效 lease（降级到
  关闭/重开合同，绝不拒绝读取）。scan 在锁外、存在性复核在锁内（TOCTOU 防护）。
