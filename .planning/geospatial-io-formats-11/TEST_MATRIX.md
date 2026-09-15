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

| # | 能力 | 测试目标 | 独立 oracle | 命令 | exit | evidence |
|---|---|---|---|---|---|---|
| T1 | param_guard escape 拒绝/红显 | test_io_param_guard | display() 契约（secret 不出现）+ typed reason 字段 | ./build-dev/test_io_param_guard | 0 | 17 assertions / 5 cases |
| T2 | finalize manifest 写出/对账 | test_io_finalize_manifest | FIPS 180-4 "abc" 向量 + 字节翻转 → digest_mismatch | 同上模式 | 0 | 52 assertions / 4 cases |
| T3 | stage_ledger attach/sweep | test_io_stage_ledger | 5 种 crash 形态 fail-closed；live 事务不被 sweep；attach 后仍 attachable | 同上 | 0 | 全过 |
| T4 | io:clip srcCrsOverride（#1001） | test_io_operators | 期望范围独立计算 margin 1e-9；refusal 无输出落盘 | 同上 | 0 | 159 assertions |
| T5 | cog_options 组合/解释 | test_io_cog_options | validateCog + GDALGetBlockSize 双确认；key 唯一性 | 同上 | 0 | 2140 assertions |
| T6 | COG deterministic | test_io_cog_options | 同参数双生成 → datasetSha256Hex 相等（同栈） | 同上 | 0 | digests equal |
| T7 | truncated/corrupt COG/GPKG | test_io_gdal_matrix | validateCog typed throw；verify digest drift；GPKG typed failure | 同上 | 0 | 36 assertions |
| T8 | vector capability 报告 | test_io_vector_interchange | 报告逐项对照 GDALGetDriverByName/DCAP 实况 | 同上 | 0 | 29 assertions |
| T9 | io:subdatasets | test_io_subdataset_inventory | netCDF 双变量 fixture（nc_create 直写）→ count=2 + 投影 4x3；driver-gated skip | 同上 | 0 | 全过 |
| T10 | metadata_patch | test_io_metadata_patch | inspectRaster READ 路径回读全部字段；7 类 refusal；manifest 连续性 | 同上 | 0 | 35 assertions |
| T11 | io:verify_dataset | test_io_operators | verified/digest_matched/issues[] codes；legacy allowed 不洗绿 | 同上 | 0 | 全过 |
| T12 | Unicode/long path/read-only | test_io_paths + 各新套件 | Unicode 目录/文件名全链路；chmod 只读 → OpenFailed | 同上 | 0 | 全过 |
| T13 | 回归 | 20 × test_io_* + test_adversarial_m3 | 既有断言不变绿改红 | loop | 0 | 21/21 suites |
| T14 | routing 回归（F3） | test_io_operators netCDF case | convert_format driver=netCDF 光栅输入 → 产出 .nc | 同上 | 0 | 全过 |

终验（Oracle 6）：上述 T1–T5/T7–T11 关键 15 套件**原样连续运行两遍，两遍全绿**（2026-09-16）。

Gate 命令模板：直接运行测试二进制（QT_QPA_PLATFORM=offscreen）；全量回归为 20 个 test_io_* 目标逐一直接运行。
