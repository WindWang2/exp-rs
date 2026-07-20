/***************************************************************************
 * satellite_products.h  —  Landsat / Sentinel-2 / MODIS discovery & stacking
 ***************************************************************************/
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

#include <array>
#include <functional>

namespace SatelliteProducts {

enum class ProductType {
    Unknown = 0,
    Landsat,
    Sentinel2,
    Modis
};

struct BandFile {
    QString path;       ///< Absolute path to band raster (or GDAL subdataset)
    QString name;       ///< Logical name e.g. "B4", "B8A", "sur_refl_b01"
    int wavelengthNm = 0; ///< Approximate centre wavelength (0 if N/A)
    int sourceBand = 1;   ///< 1-based band index inside @a path (for multi-band files)
};

struct ProductInfo {
    ProductType type = ProductType::Unknown;
    QString productId;          ///< Scene / product id
    QString metadataPath;       ///< MTL.txt, MTD_*.xml, or .hdf/.h5
    QString rootDir;            ///< Product root directory
    QString spacecraft;         ///< e.g. LANDSAT_8, Sentinel-2A, Terra/Aqua
    QString processingLevel;    ///< L1TP, L2SP, L1C, L2A, ...
    QString acquisitionDate;    ///< ISO date if known
    QMap<QString, QString> attributes; ///< Extra key/value metadata
    QVector<BandFile> bands;    ///< Discovered bands (ordered)

    // MODIS sinusoidal tile indices (h00–h35, v00–v17); -1 if unknown
    int modisTileH = -1;
    int modisTileV = -1;
};

/**
 * Discover a Landsat Collection 1/2 product from:
 *  - path to *_MTL.txt
 *  - path to scene directory containing MTL
 */
bool discoverLandsat(const QString& path, ProductInfo* out, QString* errorMessage = nullptr);

/**
 * Discover a Sentinel-2 SAFE product from:
 *  - path to .SAFE directory
 *  - path to MTD_MSIL1C.xml / MTD_MSIL2A.xml
 *  - path to parent of GRANULE (will search)
 *
 * @param preferredResolution  "10m", "20m", or "60m" (L2A R* folders); ignored for L1C flat layout
 */
bool discoverSentinel2(const QString& path, ProductInfo* out,
                       const QString& preferredResolution = QStringLiteral("10m"),
                       QString* errorMessage = nullptr);

/**
 * Discover a MODIS product from:
 *  - path to .hdf / .HDF / .h5 / .he5 (GDAL subdatasets)
 *  - path to directory containing MODIS files
 *  - path to a single MODIS-named GeoTIFF (e.g. re-exported tile)
 *
 * Parses hXXvYY tile indices from the filename when present.
 * Requires HDF4 or HDF5 GDAL drivers for native NASA HDF products.
 */
bool discoverModis(const QString& path, ProductInfo* out, QString* errorMessage = nullptr);

/**
 * Auto-detect Landsat / Sentinel-2 / MODIS from path and discover product.
 */
bool discoverProduct(const QString& path, ProductInfo* out,
                     const QString& sentinelResolution = QStringLiteral("10m"),
                     QString* errorMessage = nullptr);

/**
 * Stack selected product bands into a multi-band GeoTIFF.
 * Band order follows @p bandNames when non-empty; otherwise all non-QA bands.
 * Geotransform/projection taken from the first stacked band.
 *
 * Progress callback: fraction in [0,1], optional message.
 */
bool stackToGeoTiff(const ProductInfo& product,
                    const QStringList& bandNames,
                    const QString& outputPath,
                    QString* errorMessage = nullptr,
                    const std::function<void(double, const QString&)>& progress = {});

/**
 * MODIS sinusoidal sphere WKT (radius 6371007.181 m) used by NASA MODIS tiles.
 */
QString modisSinusoidalWkt();

/**
 * Parse MODIS tile indices from a product filename (e.g. ...h27v06...).
 * @return true if both h and v were found in range.
 */
bool parseModisTileIndices(const QString& fileName, int* tileH, int* tileV);

/**
 * Compute geotransform for a MODIS sinusoidal tile.
 * @param tileH  horizontal tile 0–35
 * @param tileV  vertical tile 0–17
 * @param width  raster width in pixels
 * @param height raster height in pixels
 * @param gt     output 6-coefficient affine (north-up, dy negative)
 */
bool modisTileGeoTransform(int tileH, int tileV, int width, int height,
                           std::array<double, 6>* gt, QString* errorMessage = nullptr);

/**
 * Assign MODIS sinusoidal georeference to a raster that lacks CRS/GT
 * (or force overwrite), writing a GeoTIFF. Does not reproject.
 *
 * @param tileH/tileV optional override; if <0, parsed from input path / productId
 */
bool assignModisSinusoidalGeoref(const QString& inputPath,
                                 const QString& outputPath,
                                 int tileH = -1,
                                 int tileV = -1,
                                 QString* errorMessage = nullptr);

/**
 * Full MODIS georeference workflow: ensure sinusoidal GT/CRS, then optionally
 * warp to @p dstCrs (e.g. "EPSG:4326"). If dstCrs is empty, only assign sinu.
 */
bool georeferenceModis(const QString& inputPath,
                       const QString& outputPath,
                       const QString& dstCrs = QStringLiteral("EPSG:4326"),
                       int tileH = -1,
                       int tileV = -1,
                       const QString& resampling = QStringLiteral("bilinear"),
                       QString* errorMessage = nullptr,
                       const std::function<void(double, const QString&)>& progress = {});

/** Human-readable product type name. */
QString productTypeName(ProductType type);

/** Default optical band set for Landsat OLI (excludes thermal/QA). */
QStringList defaultLandsatOpticalBands();

/** Default 10 m Sentinel-2 bands: B2 B3 B4 B8. */
QStringList defaultSentinel2Bands10m();

/** Default 20 m Sentinel-2 bands including red-edge / SWIR. */
QStringList defaultSentinel2Bands20m();

/** Prefer surface reflectance subdatasets for common MODIS products. */
QStringList defaultModisReflectanceBands();

} // namespace SatelliteProducts
