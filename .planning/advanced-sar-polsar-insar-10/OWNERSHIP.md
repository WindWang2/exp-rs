# OWNERSHIP — advanced-sar-polsar-insar-10

## 本 Track 独占（可写）

| 路径 | 说明 |
| --- | --- |
| `src/processing/algorithms/sar/` | SAR 内核模块（新文件 `sar_complex/polsar/insar/temporal_events/hermitian3`；已有文件仅 append/修正本 Track 契约） |
| `src/operators/rs/rs_sar_*` | SAR 算子（新增 + 既有文件 additive 修改） |
| `src/operators/rs/rs_operators_init.cpp` | append-only 注册 |
| `src/operators/CMakeLists.txt` | append-only 源文件清单 |
| `tests/test_sar_*.cpp` + `tests/CMakeLists.txt` append | 新 SAR 测试 |
| `docs/processing/sar-domain.md` | append 章节（本 Track 契约权威） |
| `data/processing/algorithm_meta/capability/rs-sar-*.json` | 新算子 sidecar（gen-meta + authored enrichment） |
| `pi/knowledge/capability-sar.md` | 仅工具再生成的产物 |
| `.planning/advanced-sar-polsar-insar-10/` | planning 证据 |

## 复用但不改（read-only 权威）

| 路径 | 权威内容 |
| --- | --- |
| `src/processing/gdal/gdal_dataset_wrapper.*` | 栅格读（`readWindowNative` 支持任意 GDAL dtype → CFloat32 通道） |
| `src/processing/gdal/gdal_multiband_block_stream.h` | `GdalStreamingOutput`（`writeTileRaw` 支持 GDT_CFloat32 typed 写）；tile 几何 |
| `src/processing/algorithms/sar/sar_metadata.h` | linear/dB 域、SICNU_* 键约定 |
| `src/processing/algorithms/sar/sar_orbit.h` | WGS84/ECEF、zero-Doppler、forward-RD（baseline 几何以 append 扩展此模块） |
| `src/geospatial/`（CRS policy、canonical metadata） | CRS/元数据约定 |
| `processing/framework/resource_estimation.h` | 内存估计约定 |

## 他 Track ownership（不碰）

| 路径 | 归属 | 本 Track 动作 |
| --- | --- | --- |
| 产品格式解析（`rs_*_import_operator.*`、`src/geospatial/products/`、SAFE/CEOS 拆包） | Track 02 | 不实现；复杂通道由通用 GDAL 栅格 + metadata 契约进入 |
| 光学时序族（`rs:temporal_*`、`src/processing/algorithms/temporal/`） | temporal platform | 只定义 fusion seam（日期/场景数组交换约定），不实现对方业务 |
| GUI 壳 / workbench / SchemaForm | workbench tracks | 算子 schema 即 UI 契约，不写 GUI 代码 |
| MCP/Pi 协议层 | agent platform | capability sidecar + knowledge pages 是唯一接触面 |

## 共享文件冲突表

| 文件 | 其他 Track 可能触碰 | 缓解 |
| --- | --- | --- |
| `src/operators/rs/rs_operators_init.cpp` | 任何新增算子的 track | 新增行集中为一次 append-only integration commit |
| `src/operators/CMakeLists.txt` | 同上 | 同上（独立 integration commit） |
| `tests/CMakeLists.txt` | 新测试的 track | append 到 SAR 测试簇附近，独立 commit |
| `.gitignore` | 新 track 白名单行 | 追加独立块，独立 commit |
| `data/processing/algorithm_meta/capability/*.json` | gen-meta 全量重生成可能触碰他族 sidecar | 只新增 `rs-sar-*` 文件；重生成后 `git status` 检查无他族 diff，有则还原他族文件 |
