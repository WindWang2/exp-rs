# REVIEW_LOG — P7 独立对抗 review（subagent #2，general-purpose 只读）+ 主 agent 自审

范围：`git diff origin/master...HEAD` 全量（50 files，+5374）。Reviewer 对 8 个新模块读全文（非仅 diff hunks），并在本机用 GDAL 3.13.3 枚举驱动能力、编译探针验证 jsoncpp 1.9.8 异常行为。

## Findings & dispositions

| ID | Sev | 位置 | 摘要 | Disposition | 修复 commit |
|---|---|---|---|---|---|
| F1 | P1 | metadata_patch.cpp | NaN-for-scale / 非法 color_interpretation 在 Phase 2 才抛 → GDAL 句柄泄漏 + eager 驱动半持久化 | **FIXED**：两检查前移 Phase 1；apply 循环 try/catch 关闭句柄后重抛 | 本 commit |
| F2 | P1 | finalize_manifest/stage_ledger/metadata_patch/io_operators | jsoncpp 类型不匹配 LogicError 逃出 GeoError-only catch（含 verifyDataset 契约违反、Phase 4 已应用后逃逸） | **FIXED**：schema_version/run_id/field/value/band 类型前置校验 + std::exception 兜底（verifyDataset→manifest_invalid；attachExisting→ledger_invalid；Phase 4→warning） | 本 commit |
| F3 | P1 | io_operators convert_format | netCDF/PDF/MBTiles 等 DCAP_RASTER+DCAP_VECTOR 双能力驱动被误路由 vector kernel，破坏"GeoTIFF→NetCDF raster export" | **FIXED**：VectorTargetCheck.alsoRaster + inputOpensAsRaster 按输入 kind 路由；回归测试 test_io_operators/netCDF routing（driver-gated） | 本 commit |
| F4 | P1 | subdataset_inventory | 远端 SDS name 原文进 JSON → 凭证泄漏 | **FIXED**：SubdatasetEntry.remoteSource；remote 条目 JSON 只出 display（selection 走 index） | 本 commit |
| F5 | P1 | param_guard | 守卫零生产调用点，docs 虚标安全控制 | **FIXED**：全部 13 个 io:* 算子接线（写目标→checkTargetPath；读源→checkSourcePath；metadata_patch 目标→checkTargetPath） | 本 commit |
| F6 | P2 | stage_ledger sweep | live 事务可被 remove=true 清掉 | **FIXED**：pass 3 读 ledger，state==staged 且 staged 存在 → `live_staged`（report only，永不 remove）；staged_file 被 live ledger 认领时同样跳过；测试改为"attachable 事务 sweep 后仍 attachable" | 本 commit |
| F7 | P2 | stage_ledger | recordStaged 接受任意目录 stagedPath → finalizeAttached 可把任意文件 rename 到目标 | **FIXED**：recordStaged 强制 staged 与 final 同目录（lexically_normal 比较）+ negative 测试 | 本 commit |
| F8 | P2 | finalize_manifest/stage_ledger | sidecar 全量读入，无上限（内存 DoS） | **FIXED**：16 MiB cap，超出 → InvalidMetadata | 本 commit |
| F9 | P3 | cog_options | header 说明的 overviews explanation 块未产出 | **FIXED**：产出 explanation["overviews"]{mode,levels_hint} | 本 commit |
| F10 | P3 | metadata_patch | warning 文案不区分 manifest 缺失/损坏 | **FIXED**：NotFound 与其它分支分文案（损坏 → 明示 stale digest 将报 mismatch） | 本 commit |
| F11 | P3 | metadata_patch | NaN nodata 读回 %.17g "-nan" 假阳性 | **FIXED**（自审 R1 一并）：实际值 stod + 双侧 isnan 比较 | 自审 commit |
| F12 | P3 | io_operators io:clip | refusal details 嵌原始 path | **FIXED**：ResourceUri display() 红显 | 本 commit |
| F13 | P3 | finalize_manifest | patch 刷新覆盖 finalized_utc | **FIXED**：保留原 publish 时间戳 | 本 commit |
| F14 | P3 | stage_ledger sweep | Windows 混合分隔符（cosmetic） | **ACCEPTED**：fs::u8path 统一处理，Linux 上为唯一路径；记录不修 | — |
| F15 | P3 | operational | 带 manifest 数据集被无 manifest 管道覆盖 → verify 变红（fail-closed，by design） | **DOCUMENTED**：interchange-11.md 新增 Republishing 段落 | 本 commit |
| R1 | (self) | metadata_patch | read-back 空 actual → stod 崩溃 | **FIXED**（自审阶段，review 前提交） | 前一 commit |
| R2 | (self) | stage_ledger | attached manifest 记录 staged 名 | **FIXED**（自审阶段） | 前一 commit |
| E-nit | P3 | cog_options | extras 在 determinism 块之后合并，extra COMPRESS=DEFLATE 不触发 LEVEL pin | **ACCEPTED**：显式 extras 即显式责任，文档已声明 replace-or-append 语义 | — |
| E-note | info | finalizeAttached | publish 成功后 ledger 写失败会报错（重试时 staged_missing fail-closed） | **ACCEPTED**：fail-closed 正确方向 | — |

## 结论
- P0 = 0（review 确认）；P1 全部修复并回归；P2 修复（F6/F7/F8）；P3 修复可修项、其余逐条 disposition 如上。
- 修复后全量回归：21 个套件（20 × test_io_* + test_adversarial_m3）全部 exit=0。
