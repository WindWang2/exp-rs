# CURRENT_ARCHITECTURE — I/O authority / seam map（Phase 0 实测 @ a5b11b7f10）

## 层次

```
operators 层   src/operators/io/io_operators.cpp   io:translate/warp/reproject/clip/
（thin adapters）                                    convert_format/build_overviews/make_cog/
                                                     vector_convert/inspect/doctor
               src/operators/io/io_fabric_operators.cpp  io:catalog_search/cube_plan/
                                                     cube_window/cache_prefetch
               注册：io_operators_init.cpp REGISTER_RS_OPERATOR + initBuiltinIoOperators()
                     （rs_operator_registry.cpp:42 call_once）
─────────────────────────────────────────────────────
geospatial 库  src/geospatial/
（sicnu_geospatial 静态库, Sicnu::Geospatial）
  convert/    raster_convert: translateRaster/warpRaster/makeCog/vectorConvert/buildOverviews
              —— 全部 staged write（stagedPathFor→finishStaged→publishStagedGroup）
  raster/     RasterReader / RasterWriter（staged、metadata-before-pixels、shape-validate finalize）
  vector/     VectorReader（流式批）/ VectorWriter（staged group、driver fail-closed）
  cog/        cogPresetOptions（5 preset authority）+ validateCog（tile/overview/compression 检查）
  metadata/   canonical_metadata：RasterMetadata/VectorMetadata/MultidimMetadata + inspect*（authority）
  multidim/   MultidimView（lazy slice）/ describeCube
  formats/    FormatRegistry：certified profiles + driverAvailable（capability authority）
  fabric/     catalog/virtual_cube/chunk_plan/query_planner/object_store（F05 owned，read-only）
  remote/     range_cache_disk、offline_gate（PROJ_NETWORK=OFF 等）
  util/       atomic_fs（stage/publish/backup/discard authority）
              resource_uri（URI 解析/红显/escape 拒绝/long path authority）
              sha256、gdal_compat.h（版本宏 authority）、time_normalization
  doctor/     data_doctor（只读诊断 + remediation 建议）
─────────────────────────────────────────────────────
runtime 层     src/runtime/chunk/*（tile_checkpoint、disk_tile_store、scratch_registry：
               chunk 级 resume authority —— G01/PR#1009 domain，本 track 只读）
workflow 层    workflow_checkpoint（step 级 checkpoint/resume authority）
```

## 本 track 新增层（P1 起）

```
src/geospatial/io/                 namespace 沿用 sicnu::geo::io（新增子 ns，additive）
  param_guard        —— operator 路径参数 → resource_uri 守卫（消费既有 authority）
  finalize_manifest  —— finalize sidecar schema v1（digest/dims/producer/options/utc）
  stage_ledger       —— runId→staged-group 日志、attachExisting、sweepOrphans
  cog_options        —— preset 之上的显式选项组合层（产出 creation options + 解释 JSON）
  vector_interchange —— FormatRegistry capability 报告（消费既有 authority）
  subdataset_inventory —— ResourceUri + canonical metadata projection
  metadata_patch     —— 白名单字段 validated patch（GDAL Update 能力 gate）
operators/io/io_operators.{h,cpp}  ← io:clip 修复(#1001)、io:subdatasets、io:metadata_patch、
                                     io:verify_dataset、io:convert_format registry 化
```

## Authority 边界（不新增第二真值）

- 路径/URI 安全：`resource_uri`（唯一）；param_guard 只是消费侧。
- digest：`sicnu::geo::Sha256`（唯一实现，有 known-answer 向量）。
- COG 合法性：`validateCog`（唯一 validator）；cog_options 只产出选项，不判断合法性。
- driver capability：`FormatRegistry` + GDAL DCAP（唯一）；vector_interchange 只聚合报告。
- metadata 词汇：`canonical_metadata`（唯一模型）；metadata_patch 只写 GDAL 元数据键并回读校验。
- chunk/workflow resume：runtime/workflow 既有模块（不在本 track 实现 scheduler）。
