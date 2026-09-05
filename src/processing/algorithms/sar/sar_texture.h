// src/processing/algorithms/sar/sar_texture.h
// GLCM texture measures for SAR amplitude imagery (Platform 3.0).
//
// Definition contract (pinned by tests):
//   * Gray-level co-occurrence matrices are computed per pixel over a
//     (kernelSize × kernelSize) window, quantized to `quantLevels` equal-width
//     bins spanning the window's [min, max].
//   * Co-occurrence counts symmetric offset pairs (p, p+d) AND (p+d, p) in the
//     given direction, then normalizes to probabilities.
//   * Measures follow Haralick's definitions (contrast, dissimilarity,
//     homogeneity, energy, entropy, mean, stddev, correlation).
#pragma once

#include <QString>
#include <QStringList>

#include <vector>

class GdalDatasetWrapper;
class GdalStreamingOutput;

namespace sicnu::sar
{

enum class GlcmMeasure
{
  Contrast,
  Dissimilarity,
  Homogeneity,
  Energy,
  Entropy,
  Mean,
  StdDev,
  Correlation,
};

/// Parses "contrast"|"dissimilarity"|"homogeneity"|"energy"|"asm"|"entropy"|
/// "mean"|"stddev"|"correlation"; ok=false on unknown tokens.
GlcmMeasure glcmMeasureFromString( const QString &token, bool *ok );
QString glcmMeasureToString( GlcmMeasure measure );

struct TextureParams
{
  int windowSize = 5;      ///< odd GLCM window (3..15)
  int quantLevels = 16;    ///< gray levels per window (2..64)
  int directionDeg = 0;    ///< 0 | 45 | 90 | 135
  int displacement = 1;    ///< offset distance in pixels
  QStringList measures;    ///< subset of measure tokens; empty = all 8
};

/// Pure per-pixel GLCM measures over one window (row-major, stride
/// windowSize; NaN handled by the caller — the tile kernel never receives
/// NaN windows). Measures are written in `values[]` in the order of
/// `measureList`.
void glcmMeasuresForWindow( const float *window, int windowSize, int quantLevels,
                            int dx, int dy, const std::vector<GlcmMeasure> &measureList,
                            float *values );

/// Streaming texture map: for each requested measure one Float32 output band
/// (band order = measureList order). @a nodata pixels produce NaN in every
/// measure. Returns false on I/O failure (caller abandons output).
bool textureRaster( const GdalDatasetWrapper &src, int band,
                    const TextureParams &params, float nodata,
                    GdalStreamingOutput &dst, int tileDim,
                    const QString &polarizations = QString(),
                    const QString &sensor = QString() );

} // namespace sicnu::sar
