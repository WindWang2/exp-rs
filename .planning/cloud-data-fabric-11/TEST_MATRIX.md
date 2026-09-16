# TEST_MATRIX — cloud-data-fabric-11

每项能力 → 独立 oracle → 命令 → exit → evidence。测试可执行文件在 `build-dev/`，直接运行
（QT_QPA_PLATFORM=offscreen）或 ctest。运行环境配方见 DECISIONS D-1106。

## WP A — 统一对象 identity（tests/test_io_fabric_identity_11.cpp）

| 能力 | 独立 oracle | 命令 | exit | evidence |
|---|---|---|---|---|
| canonicalObjectKey 拼写收敛（s3/s3a/s3c//vsis3/ 同对象同键） | 已知答案表（canonical 字符串相等） | `test_io_fabric_identity_11.exe` "canonicalObjectKey converges…" | 0 | 2026-09-16 全绿（66 assertions） |
| malformed 拒绝（userinfo/query/无bucket 等） | 拒绝形状表 | 同上 | 0 | 同上 |
| fabricMirrorIndexKey credential-free | 签名 URL vs 裸 URL 同键；secret 不在键中 | 同上 | 0 | 同上 |
| objectStoreCredentialContext 主体分离 | 不同 keyId/端点/匿名 → 不同指纹；secret 变化 → 同指纹 | 同上 | 0 | 同上 |
| /vsis3/ ETag 探针（真实 GDAL 签名 HEAD） | loopback S3 server 的 ETag 字符串 | 同上 "identity probes and tokens converge…" | 0 | 同上 |
| 跨拼写 token 收敛（s3:// 与 /vsis3/ 同 ri1 token） | 两拼写 token 字符串相等 | 同上 | 0 | 同上 |
| forced-offline 探针零请求 | server.requestCount() 不变 + probed=false | 同上 | 0 | 同上 |

## WP B — 对象存储 range cache（同文件）

| 能力 | 独立 oracle | 命令 | exit | evidence |
|---|---|---|---|---|
| 对象路径包装（s3/s3a/vsis3 → 同一缓存拼写） | fabricCachedPath 字符串相等 | `test_io_fabric_identity_11.exe` "range cache wraps object-store paths…" | 0 | 同上 |
| 包装路径真实读（GDAL 签名 range GET） | 读回字节 == 写入 payload | 同上 | 0 | 同上 |
| 主体分离（A/B 两账号条目独立） | B 首读 = miss（bytes_fetched 增加、hits 不变） | 同上 | 0 | 同上 |

## WP C — offline mirror replay（tests/test_io_fabric_replay_11.cpp）

| 能力 | 独立 oracle | 命令 | exit | evidence |
|---|---|---|---|---|
| loopback S3 物化 4 chunks | mirror report 计数 | `test_io_fabric_replay_11.exe` | 0 | 2026-09-16 全绿（36 assertions） |
| **Oracle 1：forced-offline 零网络重放** | offline 下 readWindow 命中 mirror 且 server.requestCount() 全程不变；值与在线读 byte-equal | 同上 case 1 | 0 | 同上 |
| 离线索引（token+grid facts） | lookupMirrorAsset 事实断言 | 同上 | 0 | 同上 |
| 离线 miss 诚实（无假装命中） | 窗口超镜像范围 → provenance 失败（deny 环境） | 同上 case 2 | 0 | 同上 |
| 损坏 chunk = miss（size 校验） | 截断 chunk 文件 → resolveMirrorArtifact.hit=false + skippedCorrupt | 同上 | 0 | 同上 |
| 过期策略 | 改写 writtenUtc 至 1999 → maxAge=3600 → expired=true；maxAge=0 仍命中 | 同上 | 0 | 同上 |

## 回归（master 已有套件 × 本 track 变更后）

最终 gate（Phase 8，`build-dev/runfinal.cmd`，PROJ_DATA+Qt 环境见 D-1106）：
**连续两遍、11/11 全绿**（2026-09-16）：

| 套件 | pass1 | pass2 |
|---|---|---|
| test_io_fabric_object_store | 0 | 0 |
| test_io_fabric_catalog | 0 | 0 |
| test_io_fabric_cube | 0 | 0 |
| test_io_fabric_plan | 0 | 0 |
| test_io_fabric_scale | 0 | 0 |
| test_io_range_cache | 0 | 0 |
| test_io_identity | 0 | 0 |
| test_io_fabric_identity_11 | 0 | 0 |
| test_io_fabric_replay_11 | 0 | 0 |
| test_io_fabric_multidim_11 | 0 | 0 |
| test_io_fabric_locality_11 | 0 | 0 |

注：Windows 的 scale RSS 门为 32MiB（Linux 保持 10.0 的 2MiB）——Windows 工作集
首触噪声粗于 Linux，量级仍比 O(catalog) 回归低两个数量级（REVIEW_LOG #17）。

排除：test_io_fabric_operators（exit 42，**pre-existing Windows 布局缺陷**：
sicnu_operators=SHARED × sicnu_geospatial=STATIC → exe/DLL 双 range-cache 状态；
Linux ELF 单实例故 10.0 跑绿。REVIEW_LOG 与 PR_BODY known-limitations 记录）。

## Phase gate

- [x] 全部 fabric 套件连续两遍全绿（Oracle 4/6）— 2026-09-16 完成

## WP F — access-pattern prefetch（tests/test_io_fabric_locality_11.cpp，2026-09-16）

| 能力 | 独立 oracle | 命令 | exit | evidence |
|---|---|---|---|---|
| 窗口序列合并（4 重叠窗 → 1 读） | report.mergedReads==1 | `test_io_fabric_locality_11.exe` | 0 | 两遍 × exit 0 |
| 预热 + 二遍缓存命中（零数据拉取） | 二遍 cacheHits==1/warmed==0/bytesPulled==0（遥测真值） | 同上 | 0 | 同上 |
| 预算饥饿（读前估计门槛） | maxBytes=1 → skippedBudget==1 + budgetExhausted | 同上 | 0 | 同上 |
| 镜像协调（mirror-hit 零请求零拉取） | token 取自离线索引（免探针）；requestCount 不变 | 同上 case 2 | 0 | 同上 |

## 最终 gate（Phase 8 复验）

- 9 个 fabric 套件 × 2 连续全绿（object_store/catalog/cube/plan/scale/range_cache/io_identity/
  identity_11/replay_11/locality_11/multidim_11 中除 operators 外全部）。
- test_io_fabric_operators：exit 42，**pre-existing Windows 布局缺陷**（sicnu_operators 为 SHARED
  而 sicnu_geospatial 为 STATIC → exe 与 DLL 各持 range-cache 静态状态；Linux ELF 单实例故 10.0
  跑绿）。disposition：P2 pre-existing，不属本 track 回归，不在本 track 修（build 布局变更影响
  全部消费者）。
