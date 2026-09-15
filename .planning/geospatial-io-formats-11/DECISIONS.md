# DECISIONS — F17 geospatial-io-formats-11

格式：D-NNN | 背景 | 候选 | 选择 | 理由。

## D-001 | 大幅 rescope：8 包全部落在真实缺口上，不重建既有能力
- 背景：Phase 0 审计显示 atomic writer（stage/publish/rollback/sidecars/backup）、COG presets+validator+makeCog、vector reader/writer/convert、canonical metadata、resource_uri、object_store、gdal_compat、multidim 已在 master（多数 4.0–10.0 落地）。原包 A"atomic dataset writer 2"若按字面重写=重复造轮子。
- 候选：(a) 按 prompt 字面重写；(b) 向下一层缺口深化（digest/manifest、attach/resume、COG 选项层、registry 化 capability、subdataset 操作面、metadata 写回、param 守卫、负向矩阵）。
- 选择：(b)，映射见 PLAN.md Rescope 表。依据 GOAL"Why this track now"条款与 Ownership 规则。
- 影响：包 A→finalize_manifest+stage_ledger attach 契约；包 F→stage_ledger（无 scheduler）。

## D-002 | scope 扩大：`src/geospatial/cog/**` 与 `src/geospatial/metadata/**` 允许最小 additive 修改
- 背景：Primary write scope 未列 cog/**；但包 B 的 blocksize/overview/nodata/alpha 选项与 validator 负向锚点天然属于该模块。若绕道在 io/ 里做第二份 validator = 双真值（违反 Autonomy defaults 1）。
- 候选：(a) 在 src/geospatial/io/ 组合层 + 不动 cog/；(b) cog/ 最小 additive（新函数/新检查，不改既有行为签名）。
- 选择：(b)，仅 additive；启动审计确认无 open PR 触碰 cog/**。metadata/** 预期不动（patch applier 放 io/）。
- 影响：PARALLEL_OWNERSHIP.md 已记录；PR_BODY 披露。

## D-003 | manifest/ledger sidecar 命名与 schema
- 候选：(a) `.sicnu-manifest.json` 单 sidecar、schema_version 字段；(b) GDAL 元数据域内嵌（无 sidecar）；(c) 两份（manifest + ledger 各一）。
- 选择：(a) + ledger 独立文件 `.sicnu-stage-ledger.json`（runId 级，可与 dataset 分离）。理由：sidecar 与 atomic_fs sidecars 惯例一致、JSON 对称（repo 惯例 format_version）、不污染数据集元数据域（b 会改变输出数据集字节，破坏 deterministic COG 对账）。schema v1 见 SCHEMA.md（P1 建立）。

## D-004 | metadata_patch 就地写 vs staged 全量重写
- 背景：metadata 写回。staged 全量重写对大文件代价过高且可能重编码（JPEG 有损）；就地写非原子（crash 可致元数据半更新）。
- 候选：(a) 一律 staged 拷贝重写；(b) 就地 Update + 先全量校验 + fsync + 前后 digest 记录；(c) 只支持 sidecar（不写数据集）。
- 选择：(b)，但 fail-closed：driver 无 GA_Update / 只读介质 / 字段值非法 → typed refusal 先于任何写入；白名单外字段拒绝；patch 内容先 100% 校验再施加。文档声明 crash-window 语义与 (a) 的取舍。理由：与 gdaledit/GDAL 生态一致、O(1) IO、可验证回读；crash 半更新窗口极小且元数据可用 manifest 前值恢复（verify 报 mismatch）。
- 拒绝 (c)：违背"roundtrip 写入数据集"包目标。

## D-005 | io:clip #1001 修法
- 候选：(a) srcOverride 永远设 options.sourceCrsOverride，target = 文件 CRS；(b) 与 io:reproject 完全同构（override 仅作 source 声明）。
- 选择：(b)。有 CRS 输入 + override → 按 docs 语义 override 声明源（并要求与文件 CRS 冲突时……保守：若 meta.crs.valid 且 override 给出，则以 override 为 source 声明并保持 target=源网格等价； CRS-less + override → source=override、target=source（无重投影、范围不变）。known-answer 测试独立计算期望范围/CRS。

## D-006 | deterministic COG 的承诺边界
- 选择：deterministic=true ⇒ NUM_THREADS=1、固定 DEFLATE LEVEL、固定 BLOCKSIZE、TILED=YES；字节一致承诺限定"同 GDAL/libtiff 构建内"（跨版本 libtiff 不承诺），TEST_MATRIX T6 只在本地同栈对账。诚实写进 docs。

## D-007 | 新算子命名
- 沿用 `io:` 前缀、snake_case：`io:subdatasets`、`io:metadata_patch`、`io:verify_dataset`。stage_ledger 本期只暴露 library API + sweep 经 io:doctor 类路径？——否则 surface 膨胀；候选 (a) io:stage_attach 算子 (b) 仅 library。选择 (a) 暂缓：P4 评估 CLI/agent 需求后决定，PR_BODY 如实声明 library-only（若不加算子）。
