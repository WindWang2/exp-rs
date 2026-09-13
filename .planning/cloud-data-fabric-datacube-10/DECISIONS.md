# DECISIONS — cloud-data-fabric-datacube-10

裁决记录。编号沿用全仓 DECISIONS 惯例从 D-1001 起（避免与前代 track 编号冲突）。

## D-1001 · 新模块落在 `src/geospatial/fabric/`
备选：(a) 扩散进 remote/、stac/、catalog/ 各层；(b) 新建 fabric/ 单目录。
选 (b)：10.0 的交付是"跨原语的编排层"，散进既有目录会把编排逻辑误标成原语层职责，
也让前代权威文件的 diff 变大（并发 track 冲突面）。CMake 仅 append 注册。
依据：goal-template 自动化原则 5（最小重复实现 + ownership 边界）。

## D-1002 · 对象存储不引入 SDK，走 GDAL /vsi* 家族 + profile seam
备选：(a) 引入 aws-sdk；(b) 手写 sigv4；(c) GDAL 既有 /vsis3//vsigs//vsiaz/ + 本仓
credential 注入 seam。
选 (c)：仓库 GDAL 3.13.3 已是核心依赖（ADR 0139 确定 CPL 为唯一 HTTP 实现）；
http_fetch 注释明确 CPL 拥有传输。手写签名会造出第二传输真值。credential 注入用
RAII config option 对，**文档化单线程 open 假设**（CPLSetConfigOption 为进程级），
凭据永不入 identity/display/manifest。
依据：Autonomy defaults #7（不新增依赖）；CAPABILITY_MATRIX 复用禁令。

## D-1003 · ScopedObjectStoreCredentials 的线程语义
CPL config option 是进程级。注入窗口（构造→析构）内**不得**有其他线程并发 open 任何
/vsi* 路径——这是调用方契约，头文件显式声明；违反即为调用方缺陷。理由：GDAL 无
per-open credential 参数面（open options 不覆盖全部认证键），进程级是既有现实；
把 seam 做成显式 RAII + 契约文档，比隐式全局 setter 可审计。
（Phase 2 实现时若发现 GDAL 3.13 per-prefix config option 键可用，则升级为 per-prefix
实现并在 EVIDENCE.md 记录验证命令，本条相应修订。）

## D-1004 · catalog service 的过滤分界
STAC API 能推的（bbox/intersects/datetime/collections/ids/limit/sortby）推给服务端；
platform/sensor/instruments/assetRole/cloudCover/mediaType 一律 service 层客户端过滤，
即使部分源支持服务端 query 扩展——统一语义避免"同一查询在不同源上结果集不同"，
统计字段 `serverFiltered`/`clientFiltered` 诚实分账。

## D-1005 · 虚拟立方体首版 CRS 约束
grid 与资产 CRS 不同 → typed `GeoError(Unsupported)`（消息指明两侧 EPSG + 建议
io:warp/io:reproject 预处理）。备选是内嵌完整 warp 管线——那是 gdalwarp 语义的
第二实现，超出本 track ownership（科学 kernel 不动）。后续 track 需要时在
`fabric/` 头上扩展 `ResamplePolicy`。

## D-1006 · overlap policy 默认 = 选择序 first-wins
资产选择序（quality policy 排序，见 D-1007）即优先序；窗口内第一个命中资产写像素，
后续资产只补 NoData 空洞。备选 last-wins / 混合平均——平均引入重采样语义分歧，
先不做。策略枚举进 VirtualCubeSpec（`OverlapPolicy::FirstWins`），JSON 稳定名。

## D-1007 · quality policy 排序键（确定性）
默认排序键：(cloudCover 升序，未声明排最后) → (datetimeUtc 降序=最新优先) →
(id 升序) → (输入序)。全部键可从 AssetRecord/StacItem 取得，无网络。允许调用方
显式传入资产序覆盖默认（显式序即最终序，policy 改为 passthrough）。

## D-1008 · chunk 计划"百万级"通过计数+有界枚举实现
`ChunkPlan` 持有维度/形状/切片推导出的 count（u64 乘法，溢出 → ResourceExhausted）；
物化只经 `materializeChunks( indexBegin, maxCount )` 窗口枚举。禁止返回全量 vector
的 API 形态。

## D-1009 · planner 内存契约
catalog_query 阶段逐页消费、页边界即弃（只累计选中的 ≤ sceneBudget 资产 +
常数级统计）；scale 测试以进程 RSS 增量断言 O(selected)，不是 O(catalog)。

## D-1010 · mirror 的键与 fail-closed
mirror manifest 键 = identity token（asset_identity，provable 才行）。无 token 资产
不可镜像（与磁盘层"unprovable identity never disk-cached"同一规则）。mirror 命中
以 token 匹配为准，不以路径为准（路径会变，内容身份不会）。

## D-1011 · offline 拒绝的 ErrorCode 归 NetworkError
offline_gate 既有语义（typed GeoError + refusalMessage）。fabric 层不新造
OfflineErrorCode，避免错误分类学分裂；拒绝文案必须引用 engaging flag
（SICNU_OFFLINE / --offline）。

## D-1012 · 新算子/CLI 命名
`io:catalog_search`（查询→JSON 页）、`io:cube_plan`（intent→plan JSON）、
`io:cube_window`（虚拟立方体窗口→输出栅格 + provenance JSON）、`io:cache_prefetch`
（chunk 计划→预热报告）。CLI：`data catalog search …`、`data cube plan|window …`、
`data cache prefetch …`。与 D8 已锁定的 `data cache status|clear` 语法同族扩展。

## D-1013 · F-OPS-4 处置【已执行：窄修复纳入】
Phase 6 时点 master 仍无人认领（origin/master 无新分支、issue 全 closed）、rebase 干净
→ 按预案独立 commit 窄修复：
* `WarpOptions` 增 `sourceCrsOverride` 字段；`warpRaster` 非空时写 `-s_srs`；
* `io:reproject` 将已声明的 `srcCrsOverride` 传入（此前读而不传 = #646 类死参数）；
* 回归测试移植进 `tests/test_io_operators.cpp`（4×4 无 CRS GTiff → 4326→32633，
  断言输出为米制网格 + EPSG:32633 标签——旧缺陷输出为未变换像素网格）。
套件：test_io_operators → All tests passed (95 assertions in 7 test cases)。

## D-1014 · 进程级缓存锁契约（accepted debt 声明）
RemoteRangeCache/mirror 为单进程语义。跨进程共享同一 cache 目录不在本 track 实现
（10.0 专项 G 的 process-safe 条款以此声明 + 头文件契约文档满足：写者唯一、
publish 原子、损坏可恢复）。若未来多进程需求成立，另立 track。

## D-1015 · 本地目录遍历走 GDAL VSI API，不用 std::filesystem
本机 GCC 16 快照（gcc 16.2.1+r23, Arch）存在 include-order 敏感的头损坏：
`<utility>`+`<string>` 之后 `<filesystem>` 的 std::filesystem 命名空间声明被破坏
（复现：`#include <utility>` → `#include <string>` → `#include <filesystem>` →
error: 'filesystem' is not a namespace）。同一文件在不同 TU 状态下时好时坏，
风险不可控。fabric 层的目录遍历改用 GDAL VSI 原语（VSIReadDirRecursive/VSIStatL/
CPLFormFilename 语义的本地 helper）——VSI 本就是本层传输权威（D-1002/ADR 0139 同源），
跨平台。已验证：sicnu_geospatial 全量编译通过（catalog_service.cpp.o）。
修复后回扫：其余 fabric 模块一律不引入 std::filesystem。

## D-1016 · credential RAII 的恢复语义
ScopedObjectStoreCredentials 析构时**恢复先前值**（hadPrior → 恢复，否则移除键），
恢复顺序为安装的逆序。头文件措辞相应理解为"exactly what it set"（含恢复语义）。

## D-1017 · credential 窗口关闭时清 GDAL VSICURL 句柄缓存
实测：GDAL 按 URL 缓存已认证的 /vsis3/ 句柄（VSICURL property cache）；窗口
关闭后，同 URL 的后续窗口会复用上一窗口的签名上下文——凭据残留超出作用域。
因此 ScopedObjectStoreCredentials 析构在恢复 config 键之后调用
VSICurlClearCache()（仅 GDAL 元数据缓存；/vsirangecache/ 的数据块不受影响）。
测试证据：signed 窗口后 anonymous 重开同一 URL，修复前 typed open 失败，修复后通过。
