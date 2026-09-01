/***************************************************************************
 * satellite_products.h  -  Landsat / Sentinel-2 / MODIS discovery & stacking
 ***************************************************************************/
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

#include <array>
#include <functional>

#include "data/band_role.h"

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
    int fwhmNm = 0;       ///< Approximate full-width at half-maximum (0 if N/A)
    sicnu::data::BandRole role = sicnu::data::BandRole::Unknown; ///< Semantic band role
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
 * Parse a Landsat MTL.txt into a flat key/value map (strips GROUP / END_GROUP
 * lines, removes surrounding quotes). Used by radiometric calibration to read
 * RADIANCE_MULT/ADD, REFLECTANCE_MULT/ADD, SUN_ELEVATION, K1/K2 constants.
 * @return empty map (with @p errorMessage set) if the file cannot be opened.
 */
QMap<QString, QString> parseMtlKeyValues(const QString& mtlPath, QString* errorMessage = nullptr);

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
 * Names in @p bandNames that cannot be resolved against the discovered
 * @p product bands (empty when @p bandNames is empty — all non-QA bands — or
 * when every name resolves). Import operators fail closed on a non-empty
 * result instead of silently stacking fewer bands than requested (#676).
 */
QStringList unresolvableBands(const ProductInfo& product, const QStringList& bandNames);

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

/**
 * Semantic role of a Landsat band name (B1..B11, QA_*, ST_*, SR_*), given the
 * MTL SPACECRAFT_ID (e.g. "LANDSAT_8"). OLI (Landsat 8/9) and legacy TM/ETM
 * (Landsat 4-7) band assignments differ (B1: Coastal vs Blue; B6/B7: SWIR vs
 * Thermal). An unknown spacecraft defaults to the OLI layout.
 */
sicnu::data::BandRole landsatBandRole(const QString& bandName, const QString& spacecraft);

/** Semantic role of a Sentinel-2 band name (B1..B12, B8A, SCL, MSK_*, ...). */
sicnu::data::BandRole sentinel2BandRole(const QString& bandName);

/** Semantic role of a MODIS band name (sur_refl_b01..b07, ...). */
sicnu::data::BandRole modisBandRole(const QString& bandName);

/** Default 10 m Sentinel-2 bands: B2 B3 B4 B8. */
QStringList defaultSentinel2Bands10m();

/** Default 20 m Sentinel-2 bands including red-edge / SWIR. */
QStringList defaultSentinel2Bands20m();

/** Prefer surface reflectance subdatasets for common MODIS products. */
QStringList defaultModisReflectanceBands();

// ---------------------------------------------------------------------------
// Radiometric state
//
// Dataset-level metadata recording the physical meaning of pixel values.
// Written by radiometric calibration and atmospheric correction so that
// downstream multi-date operators (change detection) can verify that two
// acquisitions are radiometrically comparable before differencing them.
// ---------------------------------------------------------------------------

/// Metadata key: "SICNU_RADIOMETRIC_STATE".
constexpr char kRadiometricStateKey[] = "SICNU_RADIOMETRIC_STATE";
/// Value domain for kRadiometricStateKey.
constexpr char kRadiometricStateRadiance[] = "radiance";
constexpr char kRadiometricStateToaReflectance[] = "toa_reflectance";
constexpr char kRadiometricStateSurfaceReflectance[] = "surface_reflectance";
constexpr char kRadiometricStateBrightnessTemperature[] = "brightness_temperature";
constexpr char kRadiometricStateDigitalNumber[] = "digital_number";

/// Metadata key: "SICNU_NUMERIC_SCALE". When present, stored pixel values are
/// scaled encodings of the quantity named by kRadiometricStateKey:
/// physical_value = stored_pixel / scale. Written at import for Level-2
/// reflectance products whose pixels are stacked verbatim (Sentinel-2 L2A:
/// the MTD BOA quantification, default 10000; Landsat C2 L2: 1 /
/// REFLECTANCE_MULT when a single uniform mult covers the stack). Absent
/// means scale 1 — stored values already carry physical units. Scale-
/// sensitive consumers (EVI/SAVI additive constants) must honour it;
/// ratio-based indices are scale-invariant and may ignore it.
constexpr char kNumericScaleKey[] = "SICNU_NUMERIC_SCALE";

/// Writes the radiometric-state dataset metadata to \p path. Returns false
/// (with \p errorMessage set) when the file cannot be opened for update.
bool setRadiometricState( const QString &path, const char *state,
                          QString *errorMessage = nullptr );

/// Reads the radiometric-state dataset metadata of \p path. Returns an empty
/// string when the file is unreadable or the state is absent.
QString readRadiometricState( const QString &path );

} // namespace SatelliteProducts
