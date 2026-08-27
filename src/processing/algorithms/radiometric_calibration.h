// src/processing/algorithms/radiometric_calibration.h — DN to physical units
#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include <cstddef>
#include <functional>

/**
 * Radiometric calibration for optical satellite imagery.
 *
 * Converts raw DN (digital number) pixel values to physical units:
 * - Radiance (W·m^-2·sr^-1·µm^-1)
 * - TOA (top-of-atmosphere) reflectance (unitless, 0..1)
 * - Brightness temperature (Kelvin, thermal bands only)
 *
 * Calibration coefficients are read from sensor metadata:
 * - Landsat Collection 1/2 MTL.txt (REFLECTANCE_MULT/ADD, RADIANCE_MULT/ADD,
 *   K1/K2 constants, SUN_ELEVATION)
 * - Sentinel-2 MTD_MSIL*.xml (BOA/RADIO quantification values, sun zenith)
 * - Generic/MODIS fallback: per-band GDAL SCALE/OFFSET metadata
 */
namespace RadiometricCalibration
{
    enum class SensorType {
        Unknown = 0,
        Landsat,
        Sentinel2,
        Generic
    };

    enum class OutputUnit {
        Radiance = 0,
        ToaReflectance,
        BrightnessTemperature
    };

    /**
     * Per-band calibration coefficients.
     * Fields not applicable to a sensor remain at their defaults
     * (gain=1, bias=0, scale=1, offset=0, k1=k2=0).
     */
    struct BandCoefficients {
        double radianceGain = 1.0;  ///< RADIANCE_MULT  (L = gain*DN + bias)
        double radianceBias = 0.0;  ///< RADIANCE_ADD
        double reflMult = 1.0;      ///< REFLECTANCE_MULT
        double reflAdd = 0.0;       ///< REFLECTANCE_ADD
        double k1 = 0.0;            ///< Thermal calibration constant K1 (W·m^-2·sr^-1·µm^-1)
        double k2 = 0.0;            ///< Thermal calibration constant K2 (K)
        double scale = 1.0;         ///< Generic scale_factor / quantification value
        double offset = 0.0;        ///< Generic add_offset
        bool hasRadiance = false;   ///< true when RADIANCE_MULT/ADD was present in metadata
        bool hasReflectance = false;///< true when REFLECTANCE_MULT/ADD was present
    };

    /**
     * Calibration metadata resolved for a stacked raster.
     * @p bands is keyed by 1-based band index into the raster.
     */
    struct CalibrationMetadata {
        SensorType sensor = SensorType::Unknown;
        double sunElevationDeg = 90.0;  ///< Sun elevation angle (degrees); meaningful only when hasSunElevation
        /// True when SUN_ELEVATION (or S2 view zenith) was actually present in
        /// metadata. The 90.0 default must never be used silently: Landsat TOA
        /// reflectance without the 1/sin(theta) factor is off by ~1.5x at 42
        /// degrees, so callers fail closed when this is false.
        bool hasSunElevation = false;
        QString spacecraft;
        QString processingLevel;
        QString acquisitionDate;
        QMap<int, BandCoefficients> bands;
    };

    /**
     * Auto-detect the sensor metadata file for a raster: scans the raster's
     * directory for a Landsat `*_MTL.txt` or Sentinel-2 `MTD_MSI*.xml`.
     * Returns the path, or empty when none is found. When both families are
     * present, prefers the one matching the raster's embedded SICNU_PRODUCT_TYPE
     * metadata (fallback: Landsat MTL).
     */
    QString autoDetectMetadataFile(const QString &rasterPath);

    /**
     * Load calibration coefficients from a sensor metadata file (MTL/MTD) or,
     * when @p metadataPath is empty, from GDAL metadata embedded in @p rasterPath.
     *
     * @p bandNames maps raster band index (1-based) -> logical band name
     *        (e.g. "B4", "B8A") used to associate MTL/MTD coefficients with the
     *        correct stacked band. When empty, coefficients are keyed by band
     *        number as read from the metadata (1-based).
     * @return true if at least one band received coefficients.
     */
    bool loadMetadata(const QString &rasterPath,
                      const QString &metadataPath,
                      const QMap<int, QString> &bandNames,
                      CalibrationMetadata *out,
                      QString *errorMessage = nullptr);

    /**
     * Convert DN to radiance: L = radianceGain * DN + radianceBias.
     */
    bool toRadiance(const float *dn, float *radiance, size_t count,
                    const BandCoefficients &c);

    /**
     * Convert DN to TOA reflectance.
     * Landsat:  rho = (reflMult*DN + reflAdd) / sin(sunElevation)
     * Sentinel-2 / generic: rho = (DN + offset) / scale
     * @param sensor selects the formula; Landsat uses reflMult/reflAdd + sun elevation,
     *               others use scale/offset (sun elevation ignored).
     */
    bool toToaReflectance(const float *dn, float *reflectance, size_t count,
                          const BandCoefficients &c, SensorType sensor,
                          double sunElevationDeg);

    /**
     * Convert DN to brightness temperature (Kelvin).
     * L = radianceGain*DN + radianceBias; T = K2 / ln(K1/L + 1).
     * Requires k1 > 0 and k2 > 0.
     */
    bool toBrightnessTemperature(const float *dn, float *temperature, size_t count,
                                 const BandCoefficients &c);

    /**
     * Apply radiometric calibration to one or more bands of a GeoTIFF and write
     * a Float32 multi-band output, preserving geotransform and projection.
     *
     * @param bandIndices  1-based raster band indices to process; empty = all bands.
     * @param method       0=Radiance, 1=TOA Reflectance, 2=Brightness Temperature
     */
    bool processFile(const QString &sourcePath, const QString &outputPath,
                     const QString &metadataPath, int method,
                     const QList<int> &bandIndices,
                     QString *errorMessage = nullptr,
                     const std::function<void(double, const QString &)> &progress = {});
} // namespace RadiometricCalibration
