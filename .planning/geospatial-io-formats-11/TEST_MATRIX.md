# TEST_MATRIX — 能力 → 独立 oracle → 命令 → exit → evidence

（P1 起逐行回填 exit/evidence；oracle 必须独立于被测实现）

| # | 能力 | 测试目标 | 独立 oracle | 命令 | exit | evidence |
|---|---|---|---|---|---|---|
| T1 | param_guard escape 拒绝/红显 | test_io_param_guard | ResourceUri::resolveAgainst 既有行为 + 期望异常类型 | ctest -R test_io_param_guard | | |
| T2 | finalize manifest 写出/对账 | test_io_finalize_manifest | 独立读 sidecar JSON + sicnu::geo::Sha256 对整个文件重算 | ctest -R test_io_finalize_manifest | | |
| T3 | stage_ledger attach/sweep | test_io_stage_ledger | crash 模拟（不 finalize 直接弃）→ attach 校验 receipt 与 driver open 一致 | ctest -R test_io_stage_ledger | | |
| T4 | io:clip srcCrsOverride（#1001） | test_io_operators（新 CASE） | CRS-less 输入 + override → 输出保留源网格（范围=输入范围），非重投影；有 CRS 输入 + override → 忽略 override 为 target，用源 CRS | ctest -R test_io_operators | | |
| T5 | cog_options 选项组合/解释 | test_io_cog（扩展） | validateCog 通过 + 期望 creation options 出现在 GTiff IMAGE_STRUCTURE/metadata | ctest -R test_io_cog | | |
| T6 | COG deterministic | test_io_cog_options | 同参数两次生成 → sha256 相等（本地 libtiff 版本内） | ctest -R test_io_cog_options | | |
| T7 | truncated/corrupt COG/GPKG | test_io_gdal_matrix | validateCog isCog=false 且 reason；GPKG open typed failure | ctest -R test_io_gdal_matrix | | |
| T8 | vector capability 报告 | test_io_vector_interchange | GDALGetDriverByName 实况对照报告字段 | ctest -R test_io_vector_interchange | | |
| T9 | io:subdatasets 清单/选择 | test_io_subdatasets | 已知 netCDF/HDF fixture（driver-gated）+ VRT 无 subdataset 负例 | ctest -R test_io_subdatasets | | |
| T10 | metadata_patch 白名单/回读 | test_io_metadata_patch | 独立 GDAL reopen 读 metadata 键断言；非白名单字段 refusal | ctest -R test_io_metadata_patch | | |
| T11 | io:verify_dataset | test_io_verify_dataset | 篡改 1 字节 → digest mismatch；删 sidecar → missing manifest | ctest -R test_io_verify_dataset | | |
| T12 | Unicode/long path/read-only | test_io_paths（扩展） | 现有惯例 + 新 seam 复用 | ctest -R test_io_paths | | |
| T13 | 回归：既有 io 套件 | test_io_atomic_failures / roundtrip_matrix / raster_contract / vector_contract / fidelity / probe / doctor / identity | 既有断言不变绿改红 | ctest -R 'test_io_' | | |

Gate 命令模板：`ctest --test-dir build-dev -R 'test_io_(param_guard|finalize_manifest|stage_ledger|operators|cog|cog_options|gdal_matrix|vector_interchange|subdatasets|metadata_patch|verify_dataset|paths|atomic_failures|raster_contract)' --output-on-failure -j1`
