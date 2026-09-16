# Cloud Data Fabric & Multidimensional Cube 11.0

> **Local evidence only; no online CI dependency.**

## Baseline & dedupe

- Baseline `origin/master@a5b11b7f10fa010c1c060864fb427d777ba9a4aa`（启动审计）。
  启动时 open PR：#1009（execution-runtime-convergence-11）、#1008（radiometric-spectral）—
  两者 changed files 与本 track **零业务文件交集**（详表 `.planning/cloud-data-fabric-11/PARALLEL_OWNERSHIP.md`）；
  共享 integration files（tests/CMakeLists.txt、.gitignore、CHANGELOG）均 append-only/minimal。
- Open issues #1001–#1007 全部为 io/workflow/dataset/agent/georef 域 R2 残留，逐条 dedupe 后
  与本 track 无关 → OUT_OF_SCOPE 登记，未修未关。
- 本机并行 worktrack 多个（teaching-lab / spectral / workflow / cartography 等兄弟 11.0 track），
  本 track 构建期间与其共享宿主（16 核/31GB），遵守单实例 -j2。

## Why

Data Fabric 10.0 留下四个已声明的缺口：s3/gs/az 对象路径不被 range cache 包装（M-R2 follow-up）、
远程资产离线镜像重放不可达（A-R5 accepted-debt：镜像查询前先网络身份探针）、multidim 计划
拒绝 CubeSlice（EO-only）、CLI/operator 面缺 mirror/prefetch（D-1012 已声明未交付）。本 PR
逐项关闭，并把 WP A 的跨协议 identity 规则作为前置。

## 架构决定（详见 .planning/cloud-data-fabric-11/DECISIONS.md D-1101..D-1106）

1. **D-1101 对象 canonical key**：s3/s3a/s3c//vsis3/ 收敛 `s3://bucket/key`（gs/az 同理）；
   强 ETag ⇒ `ri1:v1` token（复用 8.0 basis，无第二身份方案）；http(s) URL 身份保持原通道
   （无端点知识时不能与 bucket/key 合并——诚实边界）。
2. **D-1102 cache VSI-object 模式**：取数/fallback 经 VSI 栈（GDAL 签名，ADR 0139 不变）；
   资源键 = canonical + 凭据上下文指纹（非秘密形状 SHA-256 前缀 16 hex）；secret 永不入键。
3. **D-1103 mirror-first 离线重放**：manifest v2 离线索引（资产键 → token + 网格事实）；
   build 从索引取事实（免探针），readWindow 先查 mirror 再开远程；size+sha256 完整性、
   writtenUtc+maxAge 过期，全部 typed miss。
4. **D-1104 multidim 切片**：`dimensionRanges` + `perDimension`；尾轴 temporal = typed
   Unsupported（消灭静默零计划）；executeChunks 多维分支走唯一 readSliceWindow 读路径。
5. **D-1105 缺陷批次**：MirrorReport 1024 上限、预算基准统一、manifest 节流+终写、
   resolveMirrorHit manifest 快照缓存、CLI data cube window 对齐算子（A-R3/A-R6）。
6. **分层**：identity/ 层不反向依赖 fabric——对象身份装配在 fabric/object_store，
   VSI HEAD 探针下沉 remote/vsi_object_identity（transport 层）。

## 实际交付（WP A–H）

- **A** `fabric/object_store`：`canonicalObjectKey`、`isObjectStoreVsiPath`、
  `probeObjectStoreIdentity`、`objectStoreIdentityToken`、`objectStoreCredentialContext`、
  `fabricAssetIdentity`；fabric 四处调用方统一改走新入口。
- **B** `remote/range_cache`：VSI-object 条目（vsiPath+credentialContext 键）、
  VSI HEAD 身份探针与 ETag 失效、`fetchRangeVsi`、对象 fallback 重开原拼写、Stat 双模式、
  `setRangeCacheCredentialContext`；`fabricCachedPath` 包装对象路径并归一 scheme 拼写。
- **C** `fabric/mirror`：manifest v2（index + sha256 + writtenUtc）、`fabricMirrorIndexKey`、
  `lookupMirrorAsset`、`resolveMirrorArtifact`、manifest 快照缓存；`virtual_cube`：
  build 离线事实接入 + readWindow mirror-first + 顺带修复镜像分支悬垂 bandInfo 指针。
- **D/E** `fabric/chunk_plan`：multidim 真实切片（time instants / dimensionRanges /
  perDimension）、尾轴 typed 拒绝、切片映射（multidimSelection）；`query_planner`：
  `multidim{path,variable}` intent、store_open/chunk_planning 阶段、executeChunks 多维执行。
- **G** CLI `data mirror materialize|stats`、`data cache prefetch`（D-1012）、intent JSON
  新词汇；CLI data cube window 对齐算子输出。
- **H** 新测试三套：test_io_fabric_identity_11、test_io_fabric_replay_11、
  test_io_fabric_multidim_11（netCDF authored，driver-gated）；既有 10.0 loopback 设施复用。

## 兼容性

- v1 mirror manifest 完全可读（chunk 键 fallback 链：新 index 键 → 10.0 raw-path 键）；
  新写入单键（index 键），manifest 增顶层 `index` 节（token 命名空间无碰撞）。
- range cache：http(s) 行为逐字节不变；新增对象条目只在新包装路径上出现。
- `find_package(Protobuf CONFIG REQUIRED)`（root CMakeLists 一行）：模块/配置双注入在
  vcpkg 依赖链下目标冲突，CONFIG 为唯一稳定形态（vcpkg 官方建议）。
- 全部新 API additive；无既有签名变更。

## Local tests

（Phase 8 完成两遍复验后填写终表 — 见 TEST_MATRIX.md 的运行记录）

## Known limitations

- http(s) URL 身份与对象拼写身份不跨通道合并（D-1101 论证）。
- 整对象镜像（非 chunk 级）与 multidim 镜像 = follow-up。
- 缓存/镜像保持单进程语义（D-1014）。
- 公有云真实端点 not-executed（与 10.0 相同边界）；loopback S3 为云端证据标准。

## Follow-ups

- 各域消费 `fabricAssetIdentity` 替换直接 `assetIdentityToken` 调用。
- mirror 支持多维计划与整对象物化；prefetch 访问模式驱动（WP F 深化）。
- io:mirror / io:cache_prefetch 算子 schema 对齐 CLI 新面（若后续 track 需要）。
