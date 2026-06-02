# SICNU GEO RS

Professional remote sensing analysis platform built on the QGIS engine. Pure C++ (no Python at runtime).

## Features

- **Spectral Analysis:** NDVI, EVI, SAVI, NDWI, NDBI, MNDWI indices
- **Band Math:** Custom expression evaluation across raster bands
- **Atmospheric Correction:** DOS1 and DOS2 methods
- **Change Detection:** Multi-temporal image comparison
- **Mosaic:** Raster mosaic with nodata handling
- **Processing Toolbox:** 70+ algorithms (GDAL, OTB, QGIS native)
- **Layer Properties:** Raster and vector layer dialogs with statistics
- **Measurement Tools:** Geodesic distance and area measurement
- **Identify Tool:** Click-to-query pixel/feature values
- **CRS Presets:** 36 common coordinate reference systems
- **Logging:** Unified logging with file output option

## Prerequisites

- CMake 3.20+
- Qt 6.2+ (Core, Gui, Widgets, Concurrent, Network, Svg, Xml, Sql)
- GDAL 3.4+
- PROJ 8+
- GEOS 3.10+
- SQLite3, ZLIB, LibZip, ZSTD, Protobuf, CURL, PCRE2, QCA

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
ctest --output-on-failure
```

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

### AppImage

```bash
./packaging/build-appimage.sh
```

## Architecture

```
src/
├── app/           Application (main window, dialogs, widgets)
├── core/          QGIS core library (vendored)
├── gui/           QGIS GUI library (vendored)
├── native/        Platform integration
├── processing/    GDAL/OTB/QGIS algorithm providers
├── plugins/       Plugin system
└── ui/            Qt Designer forms
```

## License

GPL-2.0-or-later
