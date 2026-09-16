# CAPABILITY_MATRIX — cloud-data-fabric-11

基线 = master@a5b11b7f（Cloud-Native Data Fabric / Data Cube 10.0 已交付之上）。
状态词：**exists**（master 已有）、**partial**（有但不满足）、**gap**（缺失）、
**new-11**（本 track 新增）、**planned**（本 track 计划内）、**degraded**（有代价的降级语义）。

## WP A — 统一对象 key/identity

| 能力 | 基线状态 | 11.0 目标 |
|---|---|---|
| 本地文件 identity（li1 token） | exists（identity/asset_identity） | 保持；契约测试 |
| 远程 http(s) ETag identity（ri1） | exists（8.0 fail-closed） | 保持 |
| VSI 对象拼写（/vsis3/ 等）identity | partial（未证明与 s3:// 拼写收敛） | new-11：canonical object token，跨拼写收敛 |
| GCS generation / Azure etag 建模 | gap | new-11：version/generation 字段化 |
| 凭据独立 cache key | partial（远程 basis 剥 credential query，但 VSI/endpoint 维度未证） | new-11：key 与凭据上下文分离契约 + 测试 |
| 凭据从不进 token/log/manifest | exists（display 脱敏 + 契约） | 保持 + 新路径测试覆盖 |

## WP B — 对象存储 range cache

| 能力 | 基线状态 | 11.0 目标 |
|---|---|---|
| /vsirangecache/ 覆盖 http(s) | exists | 保持 |
| /vsirangecache/ 覆盖 /vsis3//vsigs//vsiaz/ | partial（fabricCachedPath 字符串映射存在；对象路径语义/失败路径未证） | new-11：对象路径语义 + 失败语义修复 |
| 失败 fallback 不破坏对象存储签名 | gap（fallback → /vsicurl/ 无签名风险，待证据确认） | new-11：签名保持或 typed 拒绝 |
| block 级损坏自愈 | partial（validator 资源级；block 级 checksum 在 disk 层） | new-11：内存块校验 + 自愈计数 |
| credential separation（缓存条目） | gap | new-11：键含凭据上下文指纹（非凭据） |
| LRU/预算/遥测 | exists | 保持 |

## WP C — offline mirror replay

| 能力 | 基线状态 | 11.0 目标 |
|---|---|---|
| chunk 级物化（GeoTIFF/manifest） | exists（mirror.cpp，token 键） | 保持 |
| token→local artifact 离线索引 | gap（lookup 需先 probe token） | new-11：mirror index（v2 manifest），纯本地查找 |
| 断网零网络 probe replay | gap（VirtualCube::build probe + readWindow identity probe 联网） | new-11：forced-offline 全离线路径 |
| 完整性校验（读回验证） | partial（manifest 记录；读时校验未证） | new-11：size/hash 验证 + 损坏自愈降级 |
| 过期策略 | gap | new-11：maxAge + 显式 refresh 语义 |

## WP D — GDAL multidim cube

| 能力 | 基线状态 | 11.0 目标 |
|---|---|---|
| MDArray 打开/变量/轴清单 | exists（multidim_view metadata） | 保持 + inventory JSON |
| UTC instant 解析轴 | exists（multidim_cube） | 保持 |
| by-index/by-coordinate/by-string slice | exists（readSlice*） | 保持 |
| 命名维任意组合窗口物化（cell 预算） | partial（两自由维 readSliceWindow；非空间维全固定） | new-11：slice 规格驱动的窗口物化组合 |
| dtype 保真（非 double 中间层） | partial（values 为 double + scale/offset 未应用语义） | new-11：声明保真/已知代价 |
| NoData/CRS/geotransform | exists（descriptor/canonical 规则） | 保持 + 测试 |

## WP E — multidim chunk planner

| 能力 | 基线状态 | 11.0 目标 |
|---|---|---|
| EO cube 计划（time/y/x/band + slice） | exists | 保持 |
| multidim 计划（无 slice） | exists（forMultidimDescriptor） | 保持 |
| multidim slice 切片 | gap（typed refusal，chunk_plan.h:126） | new-11：真实切片 |
| million+ chunks 有界枚举 | exists（u64 + materializeChunks） | 保持 + multidim 回归 |
| cost/bytes 声明事实估计 | partial（EO 侧 dtype 事实；multidim dtype→bytes 未证） | new-11：multidim 字节事实 |

## WP F — prefetch & locality

| 能力 | 基线状态 | 11.0 目标 |
|---|---|---|
| plan 驱动 cache 预热（预算/取消） | exists（prefetch.cpp） | 保持 |
| 访问模式驱动（窗口序列→访问计划） | gap | new-11：locality planner（去重/排序/协调） |
| mirror/cache 协调 | partial（prefetch skip mirror hit） | new-11：互斥预算 + 报告归因 |
| 100k 记录/million chunks 规模 | partial（EO 侧已证） | new-11：multidim/locality 侧证明 |

## WP G — CLI/operator parity

| 能力 | 基线状态 | 11.0 目标 |
|---|---|---|
| data catalog/inspect/identity/cache/cube plan+window | exists | 保持 |
| data mirror（materialize/stats/resolve） | gap | new-11 |
| data prefetch | gap | new-11 |
| data cube 接受 multidim descriptor intent | gap（FabricIntent 仅 catalog/records） | new-11（词汇扩展，单解析器） |
| io:fabric 算子对应覆盖 | partial（io:cube_window 等） | new-11：mirror/prefetch 算子对齐 |
| 帮助/diagnostics 同步 | exists（机制） | new-11 内容追加 |

## WP H — loopback cloud tests

| 能力 | 基线状态 | 11.0 目标 |
|---|---|---|
| S3 loopback（/vsis3/ 真实读） | exists（test_io_fabric_object_store + http servers） | 扩展 |
| forced-offline E2E | partial（offline gate 单元级） | new-11：replay 端到端零请求断言 |
| corruption 注入→自愈 | gap | new-11 |
| 100k records 计划 | exists（EO 侧 test_io_fabric_scale） | 扩展到新路径 |
| million chunks 枚举 | exists（EO 侧） | 扩展 multidim 侧 |


---

# 最终状态（Phase 6 后，review 前）

各 WP 交付即上表"11.0 目标"列的实现结果，逐项对照：

- **WP A**：canonical key + VSI HEAD 探针 + 跨拼写 token 收敛 + credential-context 指纹 —
  全部落地（test_io_fabric_identity_11，loopback S3 真实验证）。http(s) URL 身份与对象拼写
  身份**不跨通道合并**（D-1101 诚实边界）。
- **WP B**：/vsirangecache/ 包装对象路径、签名 VSI 取数/fallback、凭据分离键、逐块校验
  （disk 层既有 + mirror 层新增 sha256 记录）— 落地（同上 + test_io_range_cache 回归绿）。
  memory 层默认不加逐块校验（disk 层已校验；insert 端有代际/覆盖守卫）— 文档诚实声明。
- **WP C**：manifest v2 离线索引、mirror-first 零网络重放（Oracle 1 请求计数证明）、
  size 完整性、writtenUtc+maxAge 过期 — 落地（test_io_fabric_replay_11）。
  sha256 在重放默认不验（size 快路径），验证策略留给 read options — 已在文档声明。
- **WP D/E**：dimensionRanges/perDimension 切片、尾轴 typed 拒绝、multidimSelection 映射、
  executeChunks 多维分支、million-chunk 有界枚举 — 落地（test_io_fabric_multidim_11，
  authored-formula + GDAL 直读双 oracle）。dtype 仍经 double 承载（接受集不变，声明诚实）。
- **WP F**：prefetchAccessPattern（访问模式 → 合并 → locality 排序 → mirror/index 协调 →
  预算/取消预热）、单开走查 — 落地（test_io_fabric_locality_11）。
- **WP G**：data mirror materialize|stats、data cache prefetch（D-1012）、multidim 词汇、
  data cube window 对齐 — 落地（CLI 编入 sicnu_cli；帮助目录数据文件未新增条目 —
  capability index 的 CLI 帮助同步为 follow-up，见 REVIEW_LOG）。
- **WP H**：三套新 loopback 套件（identity/replay/locality）+ multidim 套件 + 10.0 套件
  两遍回归 — 落地。gs/az loopback 未做（az 端点仍 typed Unsupported；gs 仅 profile 归一
  覆盖）— follow-up。
