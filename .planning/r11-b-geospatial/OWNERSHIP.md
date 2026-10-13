# OWNERSHIP & DOMAIN BOUNDARIES — Direction B (Geospatial Data Fabric, Block Processing & Resilient Persistence)

## 1. 核心写权限与归属目录 (In-Scope Components for Direction B)

- `src/geospatial/**`:
  - `src/geospatial/fabric/**` (except `query_planner.cpp` modified by #1475; includes `catalog_service.*`, `chunk_plan.*`, `object_store.*`, `mirror.*`, `virtual_cube.*`, `prefetch.*`)
  - `src/geospatial/io/**` (`stage_ledger.*`, `finalize_manifest.*`, `cog_options.*`, `gdal_feature_probe.*`, `param_guard.*`, `subdataset_inventory.*`)
  - `src/geospatial/util/**` (`resource_uri.*`, `atomic_fs.*`, `sha256.*`, `time_normalization.*`)
  - `src/geospatial/remote/**` (`remote_source_validator.*`, `range_cache.*`, `range_cache_disk.*`, `http_fetch.*`, `offline_gate.*`, `vsi_object_identity.*`)
  - `src/geospatial/raster/**` (`raster_reader.*`, `raster_writer.*`)
  - `src/geospatial/vector/**` (`vector_reader.*`, `vector_writer.*`)
  - `src/geospatial/cog/**` (`cog_presets.*`, `cog_validator.*`)
  - `src/geospatial/metadata/**` (`canonical_metadata.*`)
  - `src/geospatial/catalog/**` (`asset_query.*`)
  - `src/geospatial/stac/**` (`stac_client.*`, `stac_mapper.*`)
- `src/dataset/**` (core dataset store outside the files modified by #1475):
  - `src/dataset/dataset_store.cpp`, `src/dataset/dataset_store.h`, `src/dataset/dataset_transaction.h`, `src/dataset/dataset_manifest.*`
- `src/scientific_state/**`:
  - `src/scientific_state/asset_state_diff.*`, `src/scientific_state/asset_state_resolver.*`, `src/scientific_state/catalog/*`, `src/scientific_state/gdal/*`
- `src/experiment/**`:
  - `src/experiment/reproduction_bundle.*`, `src/experiment/reproduction_bundle_import.*`, `src/experiment/capsule/*`, `src/experiment/experiment_store.*`
- 测试套件与验证目标：
  - `tests/test_io_*` (e.g., `test_io_fabric_catalog`, `test_io_atomic_failures`, `test_io_stage_ledger`, `test_io_canonical_metadata`, `test_io_paths`, `test_io_cog_options`)
  - `tests/test_dataset_*`
  - `tests/test_remote_*`
  - `tests/test_reproduction_bundle*`

---

## 2. 严禁越界与重叠规避 (Strict Exclusions & Active PR Isolation)

### A. 在途 PR #1475 保护文件（禁止同步修改，避免代码合并冲突）:
- `src/geospatial/fabric/query_planner.cpp`
- `src/data/data_manager.cpp`
- `src/data/workspace_catalog.cpp`
- `src/dataset/dataset_store_facets.cpp`
- `src/dataset/dataset_store_samples.cpp`
- `src/dataset/dataset_store_splits.cpp`
- `src/core/qgsdatasourceresolver.cpp`
- `src/operators/framework/rs_json_params.cpp`
- `src/core/radiometric_state.{cpp,h}`
- `src/operators/framework/model_catalog.{cpp,h}`
- `src/suitability/suitability_goal.cpp`, `src/suitability/suitability_report.cpp`
- `src/processing/algorithms/temporal/temporal_workspace.cpp`

**关于 PR #1475 遗留的两条 P1 Review 意见的处理准则**：
1. `CatalogService` 提前限制 `query.maxItems` 时 `truncated` 假阴性：本方向在 `src/geospatial/fabric/catalog_service.cpp` 中独立收敛，解决 `searchPage` 和 `searchAll` 的截断诚实性与 100k+ 循环死锁问题，不改动 #1475 的 `query_planner.cpp`。
2. `fileExists()` 仅正则化存在性检查而 direct `GDALOpen` 仍使用原 URL：本方向在空间 I/O 入口（`ResourceUri::parse`、`RasterReader::open`、`inspectRaster`）进行 `file://` 和本地 URI 正规化与直通解析，实现底层 I/O 对 `file://` URL 的天然稳健支持，不破坏也不修改 #1475 的 `rs_json_params.cpp`。

### B. 其它并行 Track 隔离:
- **Track A (Science & Numerical Algorithms)**: `src/analysis/**`, `src/processing/algorithms/**`, `src/operators/{rs,opencv,gdal}/**`, `src/preflight/**`
- **Track C (Runtime & Agent Execution Plane)**: `src/processing/framework/task_center.*`, `src/operators/runtime/**`, `src/python/**`, `src/sdk/exprs/**`, `src/agent/**`, `src/workflow/**`
- **Track D (UI & Teaching)**: `src/app/**`, `data/help/**`, `src/teaching_admin/**`, `src/faultlab/**`

---

## 3. 团队分工与审查门禁 (Team Roles & Separation of Duties)

1. **Scout (只读侦察与比对子代理)**: 持续核对 master 基线、PR 状态、未解决 issue，提供风险与隔离审计，完全无代码写入权限。
2. **Implementer (唯一写实现子代理 / 主代理协同)**: 依 TDD 纪律编写 RED 测试与最小实现代码，进行 GREEN 验证及性能 profile。
3. **Independent Reviewer (独立审核子代理)**: 不参与任何实现编写，待改动就绪后以 P0/P1/P2 严格审查完整 diff 与运行证据，行使一票否决权直至 P0/P1 为 0。
4. **主控 Agent (Coordinator)**: 总控编排、构建互斥锁管理、整体验收、双轮回归、创建交付 PR。
