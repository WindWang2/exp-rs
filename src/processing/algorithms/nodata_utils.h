// nodata_utils.h — shared NoData sentinel resolution and validity policy.
//
// Platform convention (Scientific Algorithms Foundation 4.0):
//   * A band's declared GDAL NoData value, cast to float, is the sentinel.
//   * A band without a declared (finite) sentinel has the NaN sentinel; NaN
//     pixels are invalid regardless.
//   * Sentinel matching is exact float-cast equality — never an epsilon
//     (an epsilon silently drops legitimate values near the sentinel).
//
// Kernels that need "the invalid-value test for this band" should resolve
// through isNoDataValue so exact sentinel matching and the non-finite rule
// cannot drift per file (#A1). Not every kernel does yet: some still inline
// an equivalent test, and a few keep a different policy on purpose
// (DN-to-radiance propagates a non-finite DN as NaN instead of stamping
// the sentinel).
#pragma once

#include <cmath>
#include <limits>

class GdalDatasetWrapper;

namespace sicnu::rs
{

/// Declared NoData sentinel of @a band (1-based) cast to float; NaN when the
/// band declares no finite sentinel. O(1) GDAL metadata read.
float bandNoDataSentinel( const GdalDatasetWrapper &ds, int band );

/// True when @a v must be treated as missing: non-finite (always invalid) or exact
/// equality with a declared, finite @a sentinel.
inline bool isNoDataValue( float v, float sentinel )
{
    if ( !std::isfinite( v ) )
        return true;
    return std::isfinite( sentinel ) && v == sentinel;
}

inline bool isNoDataValue( double v, double sentinel )
{
    if ( !std::isfinite( v ) )
        return true;
    return std::isfinite( sentinel ) && v == sentinel;
}

} // namespace sicnu::rs
