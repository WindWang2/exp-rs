# SICNU GEO RS

Professional remote sensing analysis platform built on the QGIS engine. Pure C++ desktop app. Optional embedded Python console (`-DSICNU_EMBED_PYTHON=ON`).

## Features

- **Spectral Analysis:** NDVI, EVI, SAVI, NDWI, NDBI, MNDWI indices
- **Band Math:** Custom expression evaluation across raster bands
- **Atmospheric Correction:** DOS1 and DOS2 methods
- **Change Detection:** Multi-temporal image comparison
- **Mosaic:** Raster mosaic with nodata handling
- **Image Enhancement:** Contrast stretch, spatial filtering, speckle filtering (SAR)
- **Image Fusion:** Brovey, IHS, PCA pan-sharpening
- **Terrain Analysis:** Slope, aspect, hillshade, roughness, TRI, TPI
- **Classification:** NormalBayes, SVM, K-Means with cross-validation
- **OBIA:** Object-based image analysis with OTB MeanShift segmentation
- **Georeferencer:** GCP-based georeferencing with RPC support and SIFT matching
- **Processing Toolbox:** 70+ algorithms (GDAL, OTB, QGIS native)
- **Layer Properties:** Raster and vector layer dialogs with statistics
- **Measurement Tools:** Geodesic distance and area measurement
- **Identify Tool:** Click-to-query pixel/feature values
- **CRS Presets:** 36 common coordinate reference systems
- **Logging:** Unified logging with file output option

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

**483 tests** covering core algorithms, GDAL utilities, dialog UI, OBIA pipeline, and processing framework.

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
ctest --output-on-failure
```

Requires no additional system packages — ITK 5.4 and OTB 10 are vendored in-tree.

### AppImage

```bash
./packaging/build-appimage.sh
```

## Architecture

```
src/
├── app/           Application (main window, dialogs, widgets)
├── analysis/      Analysis libraries (classification, georeferencing, segmentation)
├── agent/         AI Agent infrastructure (MCP server, STAC client)
├── core/          QGIS core library (vendored)
├── gui/           QGIS GUI library (vendored)
├── native/        Platform integration
├── processing/    GDAL/OTB/QGIS algorithm providers + shared utilities
├── plugins/       Plugin system
└── ui/            Qt Designer forms
otb_ref/           Orfeo Toolbox v10 (segmentation, learning)
itk_ref/           ITK 5.4 (image processing, via git subtree)
```

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
