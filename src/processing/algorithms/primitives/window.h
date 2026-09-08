// primitives/window.h — neighborhood window / edge-policy contract
// (Foundation 5.0, Milestone A).
//
// Every kernel that reads a neighborhood (convolution, focal statistics,
// morphology drivers, local extrema, terrain products) declares its edge
// behaviour through this contract instead of inventing per-file border
// handling. GdalBlockStream pre-replicates raster borders into the halo, so
// Replicate is the streaming default and what the existing full-frame
// kernels (ImageEnhancement spatial filters, change-detection cleanup) do.
#pragma once

#include <algorithm>
#include <cstdint>

namespace sicnu::rs::primitives
{

enum class EdgePolicy
{
  /// Out-of-raster cells read as the nearest in-raster cell (GDAL block
  /// stream halo semantics; also std::clamp border handling of the
  /// full-frame ImageEnhancement kernels).
  Replicate,
  /// Out-of-raster cells mirror across the raster edge.
  Reflect,
  /// Out-of-raster cells read as a constant (@a constantValue); the caller
  /// decides whether the constant participates in the statistic (for mean
  /// sums it usually must not — see weight rules per kernel).
  Constant,
};

struct WindowSpec
{
  /// Total odd side length (3, 5, ...); even sizes are invalid.
  int32_t size = 3;
  EdgePolicy edge = EdgePolicy::Replicate;
  double constantValue = 0.0;

  /// Kernel radius; 0 for a 1×1 window.
  int32_t radius() const { return size / 2; }

  /// Halo (in pixels) a streaming tile needs for this window under the
  /// block-stream contract: exactly the kernel radius for Replicate/Reflect
  /// (the stream replicates borders); Constant also needs the radius.
  int32_t halo() const { return radius(); }

  bool valid() const { return size >= 1 && size % 2 == 1; }

  static WindowSpec square( int32_t size, EdgePolicy edge = EdgePolicy::Replicate )
  {
    WindowSpec w;
    w.size = size;
    w.edge = edge;
    return w;
  }
};

} // namespace sicnu::rs::primitives
