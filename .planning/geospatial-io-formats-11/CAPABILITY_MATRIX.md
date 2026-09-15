# CAPABILITY_MATRIX — before → after（本 track）

| 能力 | Before（a5b11f7 baseline 实测） | After（本 track 交付） |
|---|---|---|
| staged write + rollback | ✅ atomic_fs + Raster/VectorWriter/convert | 不变（复用） |
| finalize 内容 digest | ❌ finalize 只验证 shape | ✅ manifest sidecar：sha256 + dims/CRS/options/producer/UTC；verifyDataset 重算对账 |
| attach-existing staged dataset（resume seam） | ❌ crash 后 staged 只能手工发现，writer 拒绝 attach | ✅ StageLedger：begin/attachExisting/validate/finalize/sweepOrphans（library API + io: 算子） |
| orphan staged 清理 | ❌ 无 sweep | ✅ sweepOrphans 列表 + 显式清除（含 sidecars），redacted 路径 |
| COG blocksize/overview/NoData/alpha 选项 | ❌ preset 硬编码（512/AUTO） | ✅ cog_options 显式层（pow2 blocksize、overview levels、deterministic、nodata/alpha），产出可解释 JSON |
| COG deterministic 输出 | ❌ NUM_THREADS=ALL_CPUS | ✅ deterministic 模式 NUM_THREADS=1 + 固定压缩等级 |
| corrupt/truncated COG/GPKG 负向 | ❌ 仅 truncated TIFF window-read | ✅ validator fail-closed 理由、truncated GPKG typed failure、feature-detection 报告 |
| vector capability gate | ⚠️ writer fail-closed，但 io:convert_format 硬编码名单 | ✅ FormatRegistry 查询 + capability 报告 JSON（含 GeoParquet/CSV refusal 理由） |
| subdataset 操作面 | ⚠️ 仅 io:inspect 被动列出 | ✅ io:subdatasets：safe URI、selection、canonical metadata projection |
| metadata 写回 | ❌ 仅 at-create | ✅ io:metadata_patch 白名单 validated patch（Update 能力 gate、前后 digest provenance） |
| operator 路径守卫 | ❌ 裸路径 | ✅ param_guard：escape 拒绝、long path、redacted 错误 |
| io:clip srcCrsOverride | ❌ #1001 语义 bug（当 targetCrs 用） | ✅ sourceCrsOverride 语义与 io:reproject F-OPS-4 一致 + known-answer 测试 |
| driver 缺失行为 | ✅ skip/refusal 有原因 | 保持 + capability 报告字段（Oracle 3） |
| not-supported（诚实声明） | — | 多层 GPKG 写、调度器（G01 domain）、在线对象存储集成测试（仅 loopback 已有） |

## Degradations / honest limits

- `metadata_patch` 要求 driver Update 能力（GTiff/GPKG ✅）；只读介质/无 Update driver → typed refusal，不做 staged 全量重写（避免大文件重编码），文档声明。
- `sweepOrphans` 默认 dry-run 报告，显式 `remove=true` 才删。
- deterministic COG 保证单进程单线程 + 固定选项字节一致；多线程 libtiff 内部行为不做跨平台字节级承诺（文档声明）。
