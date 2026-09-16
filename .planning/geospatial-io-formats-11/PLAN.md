# PLAN — F17 Geospatial I/O, COG & Interchange 11.0

Baseline: origin/master @ `a5b11b7f10`。所有工作在 worktree `../exp-rs-geospatial-io-formats-11`，branch `zcode/geospatial-io-formats-11`。

## Rescope（相对 prompt 原 8 包的落点，依据 BASELINE 缺口审计）

既有能力不重建：atomic_fs 全套、Raster/VectorWriter stage+publish、COG presets+validator+makeCog、canonical metadata、vector reader/writer/convert、resource_uri、object_store、gdal_compat。本 track 在其上向真实缺口深化：

| 包 | 本 track 交付（新 seam 全部落 `src/geospatial/io/` 新目录 + operator/docs/tests 接线） |
|---|---|
| A | `io/finalize_manifest.{h,cpp}`：finalize 时写 `<name>.sicnu-manifest.json` sidecar（dataset digest sha256、dims/bands/dtype/CRS/driver、creation options、producer、UTC 时间、schema version）；`verifyDataset(path)` 重算 digest 对账；RasterWriter/VectorWriter finalize 挂 manifest（默认开，可关）。 |
| F+A | `io/stage_ledger.{h,cpp}`：`StageLedger` 记录 runId→(staged group, final path, producer, receipt)；`attachExisting(finalPath|runId)` 校验 staged 文件 driver-open + receipt 形状一致后允许 finalize→publish；`sweepOrphans()` 列出/清理陈旧 `.tmp` staging（含 sidecars），全部 redacted display path。给 G01/Runtime 的 API 仅 library 级，无 scheduler。 |
| B | `io/cog_options.{h,cpp}`：preset 之上的显式选项层（blocksize ∈ {128..4096} pow2、overview levels/forcing、deterministic=NUM_THREADS=1+固定 LEVEL、nodata/alpha 声明）+ 选项→creation options 解释器；`docs/io/cog-guide.md` OVERVIEWS=ALL→AUTO 漂移修正；corrupt/truncated COG validator 负向测试。 |
| C | `io/vector_interchange.{h,cpp}`：FormatRegistry 驱动的 capability 报告（driver present? create-capable? certified profile?）+ `io:convert_format` 改为 registry 查询（替换硬编码名单），新增 GeoParquet/CSV capability-gated refusal 理由。 |
| D | `io/subdataset_inventory.{h,cpp}` + 算子 `io:subdatasets`：HDF/NetCDF/VRT subdataset 清单（safe display URI、ResourceUri 分类、embeddedLocalPath 拒绝 escape）、selection（index/name）、选中子集 canonical metadata projection。 |
| E | `io/metadata_patch.{h,cpp}` + 算子 `io:metadata_patch`：validated 白名单字段（scale/offset/unit/band role/wavelength/colorInterp/nodata/acquisition time/product stamps）先全部校验后一次施加；driver 无 Update 能力 → typed refusal；patch 前后 digest 记入 provenance manifest 更新。 |
| G | `io/param_guard.{h,cpp}`：io:* 算子路径参数统一守卫（ResourceUri parse、`..` escape 拒绝、Windows long path 规范化、redacted 错误文本）；read-only source 检查。 |
| H | `tests/test_io_gdal_matrix.cpp`：feature detection（gdal_compat 宏激活表、driver 能力表）、truncated/corrupt GTiff/COG/GPKG 负向、huge logical dataset（声明式大 dims 元数据、inspect 有界）。 |

Issue #1001（io:clip）修复 + known-answer 测试（P1，先行）。

## Phases → commits

- **P0**（本 commit）：planning artifacts + .gitignore whitelist。`chore(io11): phase 0 planning artifacts`
- **P1 契约层**：param_guard + finalize_manifest + stage_ledger 骨架（含 attach/sweep）+ 单测。commit `feat(io11): stage ledger, finalize manifest, param guard contracts`
- **P2 核心一**：#1001 io:clip 修复；cog_options 选项层 + docs 漂移修正 + corrupt COG 负向测试。commit `fix(io): io:clip srcCrsOverride semantics (#1001)` + `feat(io11): explicit COG option layer`
- **P3 核心二**：vector_interchange registry 化；subdataset_inventory；metadata_patch。commit `feat(io11): vector capability gate, subdataset inventory, metadata patch`
- **P4 surface**：io_operators 新算子接线（io:subdatasets、io:metadata_patch、io:verify_dataset、io:stage_*（如适用））、帮助数据、docs/io 同步、capability index（如 repo 有 generated help 需同步——以实际仓库机制为准，append-only）。commit `feat(io11): operator surface + docs`
- **P5 硬化**：crash/orphan/cancel/Unicode/read-only/long-path 负向与资源上界测试补齐。commit `test(io11): failure, unicode, orphan sweep hardening`
- **P6 E2E**：test_io_gdal_matrix + manifest roundtrip E2E + known-answer corpus 扩充。commit `test(io11): gdal matrix + e2e known-answer`
- **P7 review**：主 agent 全 diff review → 独立 subagent #2 对抗 review → P0/P1 修复。commit `fix(io11): review remediation`
- **P8 终验**：双验证、rebase origin/master、push、PR。commit `docs(io11): final evidence + PR body`

## 测试策略

- 新测试全部 `sicnu_add_io_test(test_io_*)`，Catch2，driver-gated skip 惯例（`GDALGetDriverByName` + WARN），fixture 运行时合成于 temp 目录。
- 独立 oracle：digest 用自实现 SHA-256 库（`sicnu::geo::Sha256`，有已知向量测试）对文件重算；COG 校验用 `validateCog`（既有 authority）；#1001 测试用期望 CRS/范围独立计算，不复用 warp 输出。
- gate：新增/受影响 `test_io_*` 目标；每日全量不跑 Qt GUI 套件。

## Oracle 达成路径

1→A 的 staged-publish + manifest verify 失败路径测试；2→shipped-fixture COG（运行时合成 + validator 参数可解释 JSON）；3→driver-gated skip + capability refusal 理由字段；4/6→P8 连续两遍 targeted suites；5→终验扫描；7→REVIEW_LOG disposition。
