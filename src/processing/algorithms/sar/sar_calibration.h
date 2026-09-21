// src/processing/algorithms/sar/sar_calibration.h
// SAR radiometric calibration + backscatter conversions (Platform 3.0).
//
// Sentinel-1 GRD convention: sigma0 = DN² / A² (A = calibration constant,
// optionally after noise subtraction L = DN² - σnoise). The kernel applies
// the SAME formula for any SAR product whose DN→backscatter relation reduces
// to a scaled squared amplitude. Radiometric State 13.0 adds the per-row LUT
// path: when the caller supplies rowA (one calibration constant per input
// row, parsed from a declared SICNU_SAR_CALIBRATION_LUT sidecar), A varies by
// row; products without a readable LUT are typed refusals at the operator
// seam, never a constant-scale guess.
//
// Backscatter conversions (constant or per-pixel local incidence θi):
//   gamma0 = sigma0 / cos θi     beta0 = sigma0 / sin θi
// All operations stream tile-by-tile (O(tile) memory) and preserve NoData.
#pragma once

#include <QString>

#include <vector>

class GdalDatasetWrapper;
class GdalStreamingOutput;

namespace sicnu::sar
{

/// Output numeric domain for calibrated/converted products.
enum class SarDomain
{
  LinearPower,
  Decibels,
};

QString sarDomainToString( SarDomain domain );

/// Per-pixel calibration transform (pure, unit-testable).
/// sigma0_linear = (DN² − noiseLinear) / (A · A)
double calibrateDn( double dn, double calibrationA, double noiseLinear );

/// Backscatter conversions (pure, unit-testable). Incidence in degrees.
double sigma0ToGamma0( double sigma0, double incidenceDeg );
double sigma0ToBeta0( double sigma0, double incidenceDeg );
double gamma0ToSigma0( double gamma0, double incidenceDeg );

/// Streaming calibration of one band: DN → sigma0 (linear or dB).
/// Writes SICNU_* SAR metadata on the output dataset. @a nodata is the input
/// sentinel (NaN when undeclared); sentinel/NaN stay NaN on output.
/// @a rowA optionally supplies one calibration constant per input row
/// (Radiometric State 13.0 LUT path): sigma0 = (DN² − noise)/rowA[row]². The
/// caller must have validated the vector length against the raster height
/// (parseCalibrationLut); an out-of-range row is a hard failure here.
/// Returns false on I/O failure (caller must abandon the output).
bool calibrateRaster( const GdalDatasetWrapper &src, int band, double calibrationA,
                      double noiseLinear, SarDomain outputDomain, float nodata,
                      GdalStreamingOutput &dst, int tileDim, int outBand = 1,
                      const QString &polarizations = QString(),
                      const QString &sensor = QString(),
                      double incidenceDeg = 0.0,
                      double headingDeg = 0.0,
                      const std::vector<double> *rowA = nullptr );

struct BackscatterConvertOptions
{
  QString fromCalibration;   ///< "sigma0" | "gamma0" | "beta0" (normalized)
  QString toCalibration;     ///< target state
  SarDomain outputDomain = SarDomain::LinearPower;
  double constantIncidenceDeg = 0.0;  ///< when ≤ 0, use incidenceRaster
  QString incidenceRasterPath;        ///< optional per-pixel incidence (degrees, band 1)
};

/// Streaming backscatter conversion of one band. Requires same grid between
/// the data and an incidence raster when one is given. Returns false on
/// I/O failure or an unsupported/unknown conversion.
bool convertBackscatterRaster( const GdalDatasetWrapper &src, int band,
                               const BackscatterConvertOptions &options, float nodata,
                               GdalStreamingOutput &dst, int tileDim,
                               const QString &polarizations = QString(),
                               const QString &sensor = QString(),
                               double headingDeg = 0.0 );

/// Per-pixel incidence angle from a local-incidence raster tile (helper used
/// by convertBackscatterRaster). Returns NaN when the pixel is invalid.
float readIncidenceTile( const GdalDatasetWrapper &incidenceDs, int xOffset, int yOffset,
                         int width, int height, std::vector<float> &buffer );

} // namespace sicnu::sar
