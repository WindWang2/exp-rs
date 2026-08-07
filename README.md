# SICNU GEO RS

Professional remote sensing analysis platform built on the QGIS engine. Pure C++ desktop app. Optional embedded Python console (`-DSICNU_EMBED_PYTHON=ON`).

## Features

- **Product Import:** Sentinel-2 / Landsat / MODIS product recognition with automatic sensor, band-role, wavelength, and calibration-metadata discovery (unified product importer)
- **Semantic Band Roles:** NDVI and friends resolve NIR/Red/SWIR by role (`SICNU_BAND_ROLE` metadata) instead of hard-coded band numbers; roles exposed to the Agent/MCP workspace
- **Radiometric Calibration:** DN → radiance / TOA reflectance / brightness temperature with automatic MTL/MTD metadata detection
- **QA / Cloud Masking:** `rs:qa_mask` (Landsat QA_PIXEL, Sentinel-2 SCL, generic bitmask) and `rs:apply_mask` (analysis-ready output with masked pixels set to NoData; same-CRS grids auto-align)
- **Atmospheric Correction:** DOS1, DOS2, QUAC with metadata-resolved coefficients
- **Geometric Processing:** Orthorectification (RPC/GCP + DEM), georeferencing with SIFT matching, reprojection with reference-grid alignment (`gdal:reproject` + `reference`)
- **Spectral Analysis:** NDVI, EVI, SAVI, NDWI, NDBI, MNDWI indices; spectral library matching (SAM angle + SID), spectrum export to library
- **Band Math:** Custom expression evaluation across raster bands
- **Change Detection:** Difference / ratio / normalized difference / CVA, Otsu / percentile / manual thresholds, morphological cleanup, area statistics, and post-classification comparison with a per-class transition matrix
- **Mosaic:** Raster mosaic with nodata handling
- **Image Enhancement:** Contrast stretch, spatial filtering, speckle filtering (SAR)
- **Image Fusion:** Brovey, IHS, PCA pan-sharpening
- **Terrain Analysis:** Slope, aspect, hillshade, roughness, TRI, TPI
- **Classification:** NormalBayes, SVM, K-Means with cross-validation; held-out accuracy (kappa, confusion matrix), per-class metrics, class-imbalance warnings, model metadata sidecar
- **Hyperspectral:** MNF, PCA, SAM/SID classification, spectral unmixing, endmember extraction (PPI), RX anomaly detection, spectral resampling, continuum removal
- **OBIA:** Object-based image analysis with OTB MeanShift segmentation
- **Processing Toolbox:** 70+ algorithms (GDAL, OTB, QGIS native) over one shared Processing Registry — the same operators power the GUI, TaskCenter DAGs, CLI, and Agent/MCP
- **Provenance:** derived-asset lineage in the Data Manager; every derived raster records source, operator, parameters, and time
- **Layer Properties:** Raster and vector layer dialogs with statistics
- **Measurement Tools:** Geodesic distance and area measurement
- **Identify Tool:** Click-to-query pixel/feature values
- **CRS Presets:** 36 common coordinate reference systems
- **Logging:** Unified logging with file output option

## Remote-Sensing Workflows

The desktop UI is task-centric: **导入产品 → 辐射定标 → QA 掩膜 → 大气校正 → 网格对齐/正射 → 应用掩膜 → 分析就绪 → 指数/分类/融合/变化检测 → 后分类比较 → 谱学分析 → 溯源**, with a reusable preprocessing DAG (`lab.preprocess.optical`: calibration → QA mask → atmospheric correction → apply mask → NDVI). The same operators run through the Processing Toolbox, TaskCenter workflows, the CLI, and the Agent/MCP interface.

## Prerequisites

**一键安装所有依赖:**

```bash
sudo ./scripts/install_deps.sh
```

支持 Arch Linux, Ubuntu/Debian, Fedora, macOS (Homebrew)。

**已 Vendor 的依赖（无需安装）:**
- QGIS core/gui 源码 (32MB)
- ITK 5.4 (156MB)
- OTB 10 (97MB)
- nlohmann_json, spatialindex, poly2tri, lazperf, TinyXML
- Catch2 (测试框架，自动下载)

**需要系统安装的依赖:**

| 依赖 | 说明 |
|------|------|
| CMake 3.20+ | 构建系统 |
| Qt 6.2+ | GUI 框架 (Core, Gui, Widgets, Svg, Tools, Multimedia) |
| GDAL 3.4+ | 地理空间 I/O |
| PROJ 8+ | 坐标转换 |
| GEOS 3.10+ | 几何运算 |
| OpenCV 4.5+ | SIFT 匹配 + 分类 (可选) |
| SQLite3, ZLIB, LibZip, ZSTD | 标准系统库 |
| Protobuf, CURL, EXPAT, PCRE2 | 标准系统库 |
| QCA | Qt 加密架构 |
| BISON, FLEX | 构建工具 |
| Python 3 | 构建脚本；仅 `-DSICNU_EMBED_PYTHON=ON` 时链接运行时 |

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./sicnu_geo_rs
```

### With Tests

```bash
mkdir build-tests && cd build-tests
cmake .. -DENABLE_TESTS=ON
make -j$(nproc)
QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

**1,386 tests** covering core algorithms, GDAL utilities, dialog UI, OBIA pipeline, TaskCenter DAG execution, and processing framework.
Headless CLI binary built at `sicnu_geo_rs_cli` with `--list` operator discovery and `--schema` inspection.

### Toolbox coverage gate

Processing Toolbox Phase 1 registers algorithms against a manifest-driven CI gate:

```bash
QT_QPA_PLATFORM=offscreen ctest -R test_toolbox_coverage --output-on-failure
```

Manifest: `data/processing/toolbox_manifest.json`. Generic CLI long-tail tools ship from `data/tools/custom/`.

### With Sanitizers (Debug)

```bash
mkdir build-asan && cd build-asan
cmake .. -DENABLE_TESTS=ON -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure
```

### Install

```bash
cmake --install . --prefix /usr/local
```

### With OTB (Vendored Segmentation & Classification)

```bash
./scripts/build_with_otb.sh build-otb
# Or manually:
mkdir build-otb && cd build-otb
cmake .. -DENABLE_TESTS=ON -DSICNU_BUILD_OTB=ON -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)  # First build takes 30-45 minutes
./scripts/bundle_otb_tools.sh "$(pwd)" "$(pwd)/.."   # stage tools/otb
```

ITK is built as static PIC libraries; OTB is built **shared** so composite CLI apps
(`TrainImagesClassifier`, etc.) share a single ApplicationEngine.

Point a normal (non-OTB) product build or CLI/tests at the bundle:

```bash
export SICNU_OTB_PATH="$PWD/build-otb/tools/otb"
# optional if not using tools/otb/otbenv.profile:
export LD_LIBRARY_PATH="$PWD/build-otb/lib:${LD_LIBRARY_PATH}"
source build-otb/tools/otb/otbenv.profile   # sets OTB_APPLICATION_PATH, PATH, LD_LIBRARY_PATH
```

LibSVM / MuParserX are auto-vendored when missing from the system.

### AppImage

```bash
./packaging/build-appimage.sh
```

## Architecture

```
src/               Application + QGIS core/gui + processing
├── app/           Main window, dialogs, widgets
├── analysis/      Classification, georeferencing, segmentation
├── agent/         MCP server, STAC client
├── core/, gui/    QGIS libraries (vendored subset)
├── processing/    GDAL/OTB/QGIS providers + utilities
└── …
data/
├── processing/    Toolbox manifest (tracked)
├── tools/custom/  Generic CLI tool JSON (tracked)
└── samples/       Lab sample datasets
docs/              Design, architecture, labs, agent notes
refs/              Optional local refs: qgis/, boost/ (gitignored)
otb_ref/           Orfeo Toolbox v10 (CMake-coupled, stays at root)
itk_ref/           ITK 5.4 via git subtree (stays at root)
external/          Header-only / small vendored C++ deps
```

Full map: [docs/repo-layout.md](docs/repo-layout.md).

### Shared Utilities

- **MathUtils** (`src/processing/algorithms/math_utils.h`): Safe division, statistics computation, normalized difference — see [docs/math-utils.md](docs/math-utils.md)
- **GDAL I/O** (`src/processing/gdal/gdal_dataset_wrapper.h`): Dataset wrapper, GeoInfo extraction, batch output writing — see [docs/gdal-utils.md](docs/gdal-utils.md)
- **Dialog Base** (`src/app/dialogs/raster_processing_dialog_base.h`): Common UI for raster processing dialogs — see [docs/dialog-base-class.md](docs/dialog-base-class.md)

### Vendored Libraries

| Directory | Source | Purpose |
|-----------|--------|---------|
| `src/core/`, `src/gui/` | QGIS (vendored subset) | Map engine, rendering |
| `otb_ref/` | OTB v10 | Segmentation, classification |
| `itk_ref/` | ITK v5.4 (git subtree) | Image processing foundation |
| `external/pdal_wrench/` | PDAL | LiDAR processing |

To upgrade ITK: `./scripts/update_itk.sh v5.4.1`

## License

GPL-2.0-or-later
