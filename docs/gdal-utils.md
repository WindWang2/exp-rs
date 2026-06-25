# GDAL I/O Utilities Reference

Shared utilities for GDAL raster I/O. Wraps common patterns into reusable functions.

## Header

```cpp
#include "processing/gdal/gdal_dataset_wrapper.h"
```

## Free Functions

### ensureGdalInit

```cpp
void ensureGdalInit();
```

Ensures GDAL drivers are registered (once per process, thread-safe). Prefer this over calling `GDALAllRegister()` directly.

### createOutputTiff

```cpp
GDALDatasetH createOutputTiff(const QString &path,
                               int width, int height, int bandCount,
                               int dtype,
                               const std::array<double, 6> &geoTransform,
                               const QString &projection,
                               QString *errorMessage = nullptr);
```

Create a standard GeoTIFF output file with LZW compression. The caller is responsible for closing the returned dataset handle.

**Parameters:**
- `path` — Output file path
- `width`, `height` — Raster dimensions in pixels
- `bandCount` — Number of output bands
- `dtype` — GDAL data type (cast to int, e.g. `GDT_Float32`)
- `geoTransform` — 6-element affine geotransform
- `projection` — WKT projection string
- `errorMessage` — If non-null, receives error description on failure

**Returns:** `GDALDatasetH` on success, `nullptr` on failure.

### extractGeoInfo

```cpp
GeoInfo extractGeoInfo(GDALDatasetH ds);
```

Extract geotransform and projection from a raw GDAL dataset handle.

**Parameters:**
- `ds` — GDAL dataset handle (can be nullptr)

**Returns:** `GeoInfo` struct with geotransform and projection. Returns defaults if `ds` is nullptr.

**Example:**
```cpp
GDALDatasetH ds = GDALOpen("input.tif", GA_ReadOnly);
GeoInfo geo = extractGeoInfo(ds);
// geo.geoTransform[0] = origin X
// geo.projection = WKT string
GDALClose(ds);
```

### writeGdalOutput

```cpp
bool writeGdalOutput(const QString &outputPath, int width, int height,
                     const std::vector<std::vector<float>> &bands,
                     const std::array<double, 6> &geoTransform,
                     const QString &projection,
                     QString *errorMessage = nullptr);
```

Write multi-band float data to a new GeoTIFF file. Creates a GeoTIFF with LZW compression and writes all bands in one call. Replaces the common pattern: `createOutputTiff` + per-band `GDALRasterIO` loop.

**Parameters:**
- `outputPath` — Output file path
- `width`, `height` — Raster dimensions
- `bands` — Vector of band data (each band = width*height floats)
- `geoTransform` — 6-element affine geotransform
- `projection` — WKT projection string
- `errorMessage` — If non-null, receives error description on failure

**Returns:** `true` on success, `false` on failure.

**Example:**
```cpp
std::vector<float> band1(w * h, 42.0f);
std::vector<float> band2(w * h, 100.0f);
std::vector<std::vector<float>> bands = {band1, band2};

GeoInfo geo = extractGeoInfo(srcDs);
QString error;
bool ok = writeGdalOutput("output.tif", w, h, bands,
                           geo.geoTransform, geo.projection, &error);
```

## Types

### GeoInfo

```cpp
struct GeoInfo {
    std::array<double, 6> geoTransform = {0, 1, 0, 0, 0, 1};
    QString projection;
};
```

Geotransform and projection info extracted from a GDAL dataset.

## GdalDatasetWrapper Class

RAII wrapper around GDAL C API for raster dataset access. Move-only (no copy).

```cpp
GdalDatasetWrapper ds;
if (ds.open("input.tif")) {
    int w = ds.width();
    int h = ds.height();
    int bands = ds.bandCount();

    std::vector<float> buf(w * h);
    ds.readBandData(1, buf.data(), w, h);

    float pixel;
    ds.readPixel(1, 100, 200, &pixel);
}
// ds automatically closed on destruction
```

### Methods

| Method | Description |
|--------|-------------|
| `open(path)` | Open a raster file. Returns true on success. |
| `close()` | Close the dataset (also called by destructor). |
| `isValid()` | True if a dataset is currently open. |
| `width()` | Raster width in pixels. |
| `height()` | Raster height in pixels. |
| `bandCount()` | Number of bands. |
| `driverName()` | GDAL driver short name (e.g. "GTiff"). |
| `projection()` | WKT projection string. |
| `geoTransform()` | 6-element affine geotransform. |
| `readBandData(bandNum, buffer, w, h)` | Read an entire band into a float buffer. |
| `readPixel(bandNum, x, y, value)` | Read a single pixel value. |
| `bandNoDataValue(bandNum, hasNodata)` | Get the no-data value for a band. |
| `lastError()` | Get the last error message. |

## Common Patterns

### Read, Process, Write

```cpp
GdalDatasetWrapper src;
if (!src.open("input.tif")) return false;

int w = src.width(), h = src.height();
std::vector<float> input(w * h), output(w * h);
src.readBandData(1, input.data(), w, h);

// Process...
for (size_t i = 0; i < input.size(); ++i)
    output[i] = input[i] * 2.0f;

// Write output
GeoInfo geo = extractGeoInfo(src.handle());
std::vector<std::vector<float>> bands = {output};
writeGdalOutput("output.tif", w, h, bands, geo.geoTransform, geo.projection);
```

### Extract Band from Multi-band Raster

```cpp
GdalDatasetWrapper src;
src.open("multiband.tif");
int w = src.width(), h = src.height();

std::vector<float> band3(w * h);
src.readBandData(3, band3.data(), w, h);

GeoInfo geo = extractGeoInfo(src.handle());
writeGdalOutput("band3.tif", w, h, {band3}, geo.geoTransform, geo.projection);
```
