// primitives/morphology.h — binary morphology over 0/1/255 byte masks
// (Foundation 5.0, Milestone A).
//
// Mask encoding (platform convention, shared with the change-detection
// cleanup path):
//   1   = foreground (the feature being morphed)
//   0   = background
//   255 = NoData — protected: never modified. NoData cells are not
//         foreground, so they do not support a neighbour under erode, and
//         dilate never grows into them.
//
// Border policy: out-of-raster cells act as *foreground* for erode (border
// foreground cells survive) and as *background* for dilate (features do not
// grow inward from outside the raster). This matches the historical
// change-detection cleanup passes exactly.
//
// Full-frame contract: these kernels materialize src and dst masks (1 B/px
// each). Mask rasters on this platform are already full-frame byte maps
// (threshold/change cleanup convention); callers needing tile-streamed
// morphology over continuous rasters must not use this header directly.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sicnu::rs::primitives
{

enum class Connectivity
{
  Four,
  Eight,
};

/// Foreground value ("v == 1") shrinks: a foreground cell survives only when
/// every in-raster neighbour (4- or 8-connected) is still foreground.
/// @a src and @a dst may not alias; both are @p width × @p height bytes.
void erode( const uint8_t *src, uint8_t *dst, int width, int height,
            Connectivity conn = Connectivity::Eight );

/// Foreground grows into background cells that touch (4-/8-connected) a
/// foreground cell. NoData cells are never grown into.
void dilate( const uint8_t *src, uint8_t *dst, int width, int height,
             Connectivity conn = Connectivity::Eight );

/// Open = erode followed by dilate (removes specks smaller than the
/// structuring element, restores extent). @a scratch must be
/// @p width × @p height bytes and must not alias @a mask.
void open( uint8_t *mask, uint8_t *scratch, int width, int height,
           Connectivity conn = Connectivity::Eight );

/// Close = dilate followed by erode (fills holes smaller than the element).
void close( uint8_t *mask, uint8_t *scratch, int width, int height,
            Connectivity conn = Connectivity::Eight );

/// In-place iteration helpers (negative @a iterations are invalid → false).
bool erodeN( uint8_t *mask, uint8_t *scratch, int width, int height, int iterations,
             Connectivity conn = Connectivity::Eight );
bool dilateN( uint8_t *mask, uint8_t *scratch, int width, int height, int iterations,
              Connectivity conn = Connectivity::Eight );

} // namespace sicnu::rs::primitives
