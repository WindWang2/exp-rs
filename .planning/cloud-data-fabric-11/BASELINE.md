# BASELINE — cloud-data-fabric-11

启动审计时间：2026-09-16（本地），全部命令在主仓库 `C:\Users\wangj.KEVIN\projects\exp-rs` 只读执行。

## origin/master 事实（启动时刷新）

- `git rev-parse origin/master` = **`a5b11b7f10fa010c1c060864fb427d777ba9a4aa`**
  （prompt 生成快照为 `ebcafb4d`，已过期 — master 前进了 12 个提交）。
- Prompt 快照中的 open PR **#991（D18 workbench）与 #992（D19 foundry/benchmark）均已合并进 master**：
  - `c5d4aafe` D18: Unified Mission Workbench — MissionContext + D14/D15/D17 mounts (#991)
  - `1cea9892` Merge branch 'grok/dataset-foundry-benchmark-d19' (#992)
- master 新增（相对快照）：
  - `77e178ac` fix(ci): macOS/Windows compile fixes in d17 and Win32 paths (#993)
  - `a5b11b7f` fix: fail-closed fixes for review issues #994–#999 (#1000)

## Open PRs（启动时）

| PR | head | mergeState | 与本 track 交集 |
|---|---|---|---|
| #1009 execution-runtime-convergence-11 | `zcode/execution-runtime-convergence-11` | UNSTABLE | **零文件交集**（其变更集中在 `src/runtime/**`、`src/operators/framework/*`、`src/processing/framework/*`、workflow/agent build-unblock）。其 body 声明 master@a5b11b7f 在本机 MSVC 上 `src/workflow/pipeline_run_coordinator.cpp`（缺 `<fcntl.h>`）、`src/agent/data_platform_tools.cpp`（`BenchmarkService` 未限定）、`tests/test_large_scale_execution_10.cpp`（POSIX `unsetenv`）编译失败 — 这三个文件均属 #1009 变更集，本 track 不碰；若本 track 构建被其阻断，按"最小 build-unblock"处理并记录 DECISIONS。 |
| #1008 radiometric-spectral-workbench | `zcode/radiometric-spectral-workbench` | DIRTY | **零文件交集**（src/agent/spatial_tools、src/analysis、src/app/widgets、src/core、src/processing/algorithms）。共享文件仅 `.gitignore`、`tests/CMakeLists.txt`、各 CMakeLists — append-only。 |

## Remote branches（启动时，按 committerdate）

- `origin/zcode/execution-runtime-convergence-11`（= PR #1009 head）
- `origin/zcode/radiometric-spectral-workbench`（= PR #1008 head）
- `origin/zcode/teaching-lab-platform-11`、`origin/zcode/scientific-workflow-compiler-11`（都指向 master HEAD a5b11b7f — 尚无独立提交的兄弟 11.0 track 分支名）

## Open issues（启动时 #1001–#1007，全部为 R2 review 残留）

| # | 域 | 判定 |
|---|---|---|
| 1001 | io:clip srcCrsOverride 当 targetCrs（silent wrong clip） | io operators 域，非 fabric；不在 primary write scope（`src/operators/io/io_operators.cpp` 的 clip 逻辑），不改（避免与他 track 冲突）→ OUT_OF_SCOPE 登记 |
| 1002 | workflow makeRegistryNodeExecutor fail-open | workflow 域 → OUT_OF_SCOPE |
| 1003 | dataset joinFeaturesBySampleId JSON-null | D19 域 → OUT_OF_SCOPE |
| 1004 | agent dataset:qa scan_capped identity | D19/agent 域 → OUT_OF_SCOPE |
| 1005 | georef mapPickToLayerCrs 异常吞掉 | workbench/georef 域 → OUT_OF_SCOPE |
| 1006 | workflow PipelineRunCoordinator syntheticExecute 默认 | workflow 域（#1009 亦关注）→ OUT_OF_SCOPE |
| 1007 | dataset:qa CRS audit 缺失 | D19 域 → OUT_OF_SCOPE |

无任何 open issue 落在 `src/geospatial/fabric/**`、multidim、range cache、mirror、CLI data surface。

## ISSUES.md 判定

`ISSUES.md` 是 D3（lab content expansion）时代的算子缺口 backlog（T-1..T-3/S-1..S-2/H-1..H-3/C-1..C-2）。
逐条对照 CHANGELOG：T-1（temporal_monitor scenes）、T-2（temporal_regularize）、T-3（temporal_harmonic_breaks）、
C-2（temporal_extract_regions）已被 10.0 temporal track 修复；其余为 SAR/高光谱/制图域，不属于本 track
write scope。**不作为本 track backlog。**

## Data Fabric 10.0 已交付（本 track 的起点，见 CHANGELOG "[Data Fabric 10.0]" 节与代码）

- `fabric/object_store`：s3/s3a/s3c→/vsis3/、gs→/vsigs/、az→/vsiaz/ profile 表；`resolveObjectStore`；
  RAII `ScopedObjectStoreCredentials`（精确恢复 + VSICURL 缓存擦除；D-1003 进程序列化）；
  `fabricCachedPath`；offline typed refusal。
- `fabric/catalog_service`：本地 STAC 树 / 远端 STAC API / 内存 records 统一 CatalogQuery；有界分页；cancel。
- `fabric/virtual_cube`：懒 EO cube；bounded probe 网格协商；FirstWins；窗口读 + per-asset provenance；mirror 偏好读取。
- `fabric/chunk_plan`：命名维度 time/y/x/band；u64 chunkCountTotal；`materializeChunks(begin,max)` 有界枚举；
  `forMultidimDescriptor`（**非默认 CubeSlice 是 typed refusal — 显式 follow-up**，chunk_plan.h:126）。
- `fabric/query_planner`：FabricIntent→FabricPlan 五 stage；cost hints；`executeWindow`/`executeChunks`。
- `fabric/prefetch`：plan 驱动的 range cache 预热（maxBytes/cancel/mirror skip）。
- `fabric/mirror`：chunk 级物化（token 键 + manifest.json + chunks/<sha16>.tif 原子发布）；
  `resolveMirrorHit`；single-writer lock（D-1014）。
- `src/geospatial/identity/asset_identity`：local（li1）/remote ETag（ri1）统一 token；"" = unprovable fail-closed。
- `src/geospatial/remote/range_cache`：/vsirangecache/ VSI handler；LRU；coalescing；validator 策略；
  9.0 disk block 层（content-identity keyed）。
- CLI `data`：inspect/doctor/probe/capabilities/product describe/stac/catalog/cube plan+window/identity/cache。
- 测试：test_io_fabric_object_store（**真实 /vsis3/ loopback**，链接 ws2_32）、test_io_fabric_catalog（http_stac_server/http_range_server loopback）、test_io_fabric_cube/plan/scale/operators。

## 本机构建环境事实

- 预设：CMakePresets.json `dev-default`（configure/build/test）系列；build 资源硬上限 -j2，测试 -j1。
- MSVC/Ninja host；`QT_QPA_PLATFORM=offscreen`。
- （#1009 body 声明的 master 编译阻断见上表 — 本 track 将在首次构建时自行验证并记录真实结果。）

## Worktree

- 路径 `C:\Users\wangj.KEVIN\projects\exp-rs-cloud-data-fabric-11`
- branch `zcode/cloud-data-fabric-11` @ `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`（= origin/master）
