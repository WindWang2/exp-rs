// src/processing/algorithms/sar/sar_ratio.h
// SAR ratio / log-ratio of co-registered scene pairs (Platform 3.0).
//
// ratio = A / B (B == 0 → NoData), logRatio = 10·log10(A / B).
// Inputs may be linear power or dB (declared or explicit): dB inputs are
// converted to linear before the ratio, so the OUTPUT domain is independent
// of the input domain. Streams tile-by-tile, O(tile) memory.
#pragma once

#include <QString>

class GdalDatasetWrapper;
class GdalStreamingOutput;

namespace sicnu::sar
{

enum class RatioOutput
{
  Ratio,        ///< linear power ratio A/B
  LogRatio,     ///< 10·log10(A/B)
  LogDifference, ///< |10·log10(A) − 10·log10(B)| (dB magnitude of change)
};

QString ratioOutputToString( RatioOutput output );

struct RatioParams
{
  RatioOutput output = RatioOutput::LogRatio;
  bool inputIsDb = false;   ///< when false, inputs are linear power
};

/// Streaming ratio of band @a bandA of A and band @a bandB of B (same grid
/// enforced). @a nodataA/@a nodataB are the declared sentinels (NaN when
/// undeclared). Returns false on I/O failure or grid mismatch.
bool ratioRaster( const GdalDatasetWrapper &a, int bandA, const GdalDatasetWrapper &b,
                  int bandB, const RatioParams &params, float nodataA, float nodataB,
                  GdalStreamingOutput &dst, int tileDim,
                  const QString &polarizations = QString(),
                  const QString &sensor = QString() );

} // namespace sicnu::sar
