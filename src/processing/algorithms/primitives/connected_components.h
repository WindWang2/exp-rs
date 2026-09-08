// primitives/connected_components.h — connected-component labeling over
// 0/1/255 byte masks (Foundation 5.0, Milestone A).
//
// Mask encoding matches primitives/morphology.h: 1 = foreground, 0 =
// background, 255 = NoData. Only foreground cells are labeled.
//
// Labeling contract:
//   * labels are 1-based and compact (1..componentCount), 0 = not foreground;
//   * components are numbered in raster order of their first (top-left-most)
//     pixel, which makes the labeling deterministic and testable;
//   * the algorithm is a two-pass union-find with path compression — O(N α(N));
//   * full-frame contract, same as primitives/morphology.h (1 int32/px for
//     the label map, caller-provided).
#pragma once

#include "morphology.h" // Connectivity

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sicnu::rs::primitives
{

struct Labeling
{
  std::vector<int32_t> labels; ///< width*height, 0 = not foreground
  int32_t componentCount = 0;  ///< number of components (labels are 1..count)
};

/// Labels the foreground (value == 1) cells of @a mask (@p width × @p height
/// bytes). Returns an empty Labeling (componentCount == 0) on null/empty
/// input — which is indistinguishable from an all-background mask, as intended.
Labeling labelComponents( const uint8_t *mask, int width, int height,
                          Connectivity conn = Connectivity::Eight );

/// Per-component foreground cell counts (size == componentCount, index
/// label-1). Empty for an empty Labeling.
std::vector<int32_t> componentAreas( const Labeling &labeling );

/// Sieve: removes foreground components smaller than @a minAreaPixels by
/// clearing them to background in @a mask (in place). NoData cells are never
/// touched. Returns the number of removed pixels.
size_t removeSmallObjects( uint8_t *mask, int width, int height,
                           int64_t minAreaPixels,
                           Connectivity conn = Connectivity::Eight );

} // namespace sicnu::rs::primitives
