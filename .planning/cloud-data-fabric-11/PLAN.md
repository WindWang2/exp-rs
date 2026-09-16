# PLAN — cloud-data-fabric-11

事实基点：master@a5b11b7f；详细缺口证据见 CURRENT_ARCHITECTURE.md 与 EVIDENCE.md。
原则：每个 WP = 现状证据 → 设计取舍（DECISIONS）→ 最小 vertical slice → known-answer/negative
test → failure/cancel/resource handling → 文档/surface 同步 → 原子 commit。

## Phase 1 — authority/契约层（WP A）

**目标**：跨协议（http/s3/gs/az、/vsi* 拼写）统一对象 identity 规则，credential-independent。

1. 审计 `asset_identity.cpp` + remote identity 的 basis 构造（URL 规范化到哪一步）。
2. `fabric/object_store` 增加 canonical object token：scheme-agnostic 的 (provider, bucket, key)
   规范形态；`s3://b/k`、`/vsis3/b/k`、S3-compatible endpoint URL 在同一 profile+endpoint 下收敛。
3. ETag/version/generation 捕获扩展（VSI 的 ETag/stat 字段；GCS generation、Az blob etag）。
4. cache-key basis 与凭据上下文分离的契约测试（同对象不同凭据 → 同 token；token/basis/log 无凭据形状）。
5. 测试：known-answer（拼写收敛表）+ negative（无凭据泄漏、unprovable fail-closed）。

## Phase 2 — 对象存储缓存 + 离线镜像（WP B + WP C）

**WP B**：/vsirangecache/ 覆盖 VSI object paths：
- 验证 fallback 语义（/vsis3/ 经 cache 失败时不得降级到无签名的 /vsicurl/）——按证据决定修法；
- block 级 checksum/损坏自愈（corrupt block → 丢弃 + 重取 + 计数，typed telemetry）；
- credential separation：同一 URL 不同凭据窗口不共享缓存条目（键含凭据上下文指纹而非凭据）。
**WP C**：offline mirror replay：
- 新 `fabric/mirror_index`（token→local artifact 索引：manifest v2，向后兼容读 v1）；
- `resolveMirrorArtifact(mirrorDir, token)` 纯本地查找（零网络探测）；
- `VirtualCubeReadOptions.mirrorDirectory` 读取路径改造：离线模式下先索引命中，未命中才走
  原 probe 逻辑；forced-offline 时完全跳过 probe；
- 完整性（文件 hash/size 校验）+ 过期策略（manifest 时间戳 + 显式 maxAge）；
- Oracle 1：forced-offline 远程 replay 零网络访问（测试用 loopback server 断言零请求）。

## Phase 3 — GDAL multidim + chunk planner（WP D + WP E）

**WP D**：`multidim` 维度切片与窗口物化：
- MultidimView/Descriptor 增量：dimension inventory JSON（含 type/unit/值边界）；
- time/y/x/band（及任意命名维）slice → `readSliceWindow` 组合的窗口物化（cell 预算）；
- dtype/NoData/CRS(geotransform) 语义对齐 canonical metadata 规则。
**WP E**：`forMultidimDescriptor` 支持 CubeSlice（移除 typed refusal，转为真实切片）：
- 切片后 dims/count 重算；u64 溢出拒绝保留；
- cost/bytes：dtype×cells（已声明事实）+ slice 影响的 honest 报告；
- million+ chunks 有界枚举回归（不物化全计划——已有契约，补 multidim 侧测试）。
- Oracle 3：multidim slice known-answer 与 GDAL 直读 reference 一致（独立 oracle：直接 GDAL
  MDArray API 读同一窗口比对）。

## Phase 4 — surface/integration（WP G）

- CLI `data`：`data mirror materialize|stats|resolve`、`data prefetch`、`data cube` 支持
  multidim descriptor spec（统一 fabricIntentFromJson 词汇扩展，无第二解析器）；
- operator 面：io_fabric_operators 增补对应算子（沿用现有 schema 注册模式）；
- 帮助/diagnostics/capability index 同步。

## Phase 5 — prefetch/locality 硬化（WP F）

- 访问模式驱动的 prefetch：窗口序列输入 → chunk/range 访问计划（去重、排序、mirror 命中跳过、
  预算/取消）；
- mirror/cache 协调：mirror 命中的 chunk 不占 cache 预算；prefetch 报告区分来源；
- 规模硬化：bounded logical scale（million-chunk 计划、100k 记录）＋资源记录（PERFORMANCE.md）。

## Phase 6 — E2E/loopback（WP H）

- S3-compatible loopback（复用 tests/support/http_* server 模式 + GDAL /vsis3/）：
  - forced-offline replay E2E（Oracle 1 的端到端形态）；
  - corruption 注入 → cache/mirror 自愈；
  - 100k catalog records 计划内存有界；
  - million-chunk plan 枚举有界。
- 全部 fabric suites 连续两遍（Oracle 4/6）。

## Phase 7 — 对抗 review（subagent #2）+ 全部 P0/P1 修复

## Phase 8 — 最终双验证、rebase origin/master、push、PR（不 merge）

## 每阶段出口条件

- targeted `ctest -R "test_io_fabric|test_multidim|test_object_identity"` -j1 全绿（gate 两遍）；
- `git status --porcelain` 干净；原子 commit；`git fetch && git rebase origin/master`（冲突按
  PARALLEL_OWNERSHIP 裁决）；
- DECISIONS/EVIDENCE/TEST_MATRIX 同步更新。
