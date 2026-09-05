// src/processing/algorithms/sar/sar_speckle.h
// SAR speckle filtering (Platform 3.0).
//
// The Lee / enhanced-Lee / Frost / Kuan / Gamma-MAP kernels are the existing
// streaming tile kernels in ImageEnhancementStreaming (formula-replicas of the
// full-frame filters) — reused here verbatim, no duplicate math. This module
// adds the operator-facing driver, a refined-Lee kernel (8-direction edge
// detection + minimum-mean-square-error), and the multi-temporal speckle
// filter (Quegan-style temporal averaging with deviation gating).
#pragma once

#include "processing/gdal/gdal_block_stream.h" // Tile geometry (refinedLeeTile signature)

#include <QString>
#include <QStringList>

#include <vector>

class GdalDatasetWrapper;
class GdalStreamingOutput;

namespace sicnu::sar
{

enum class SpeckleMethod
{
  Lee,
  EnhancedLee,
  Frost,
  Kuan,
  GammaMap,
  RefinedLee,
  Multitemporal,
};

QString speckleMethodToString( SpeckleMethod method );
/// Parses "lee"|"enhanced_lee"|"frost"|"kuan"|"gamma_map"|"refined_lee"|
/// "multitemporal"; ok=false on unknown tokens.
SpeckleMethod speckleMethodFromString( const QString &token, bool *ok );

struct SpeckleParams
{
  SpeckleMethod method = SpeckleMethod::Lee;
  int kernelSize = 3;        ///< odd window size (3..15)
  double noiseVariance = 0.25;   ///< Lee/Kuan/Gamma-MAP model noise variance
  double dampingFactor = 1.0;    ///< Frost exponential damping
  int looks = 1;                 ///< equivalent number of looks (refined Lee / multitemporal)
  double deviationK = 1.0;       ///< multitemporal gate: k · localStd
};

/// Per-pixel refined-Lee tile kernel (own formula, matches the published
/// algorithm: 8-direction edge detection chooses the non-edge window whose
/// MMSE estimate replaces the center pixel).
void refinedLeeTile( const GdalBlockStream::Tile &tile, const float *haloBuf, float *coreOut,
                     int kernelSize, int looks );

/// Streaming speckle filter of one band into a Float32 output.
/// @a companionPaths: co-registered scene paths for Multitemporal (each
/// contributes band @a band, same grid required).
/// Returns false on I/O failure or grid mismatch (caller abandons output).
bool speckleRaster( const GdalDatasetWrapper &src, int band,
                    const SpeckleParams &params, float nodata,
                    const QStringList &companionPaths,
                    GdalStreamingOutput &dst, int tileDim, int outBand = 1,
                    const QString &polarizations = QString(),
                    const QString &sensor = QString() );

} // namespace sicnu::sar
