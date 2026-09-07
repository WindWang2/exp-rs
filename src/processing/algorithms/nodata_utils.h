// nodata_utils.h — shared NoData sentinel resolution and validity policy.
//
// Platform convention (Scientific Algorithms Foundation 4.0):
//   * A band's declared GDAL NoData value, cast to float, is the sentinel.
//   * A band without a declared (finite) sentinel has the NaN sentinel; NaN
//     pixels are invalid regardless.
//   * Sentinel matching is exact float-cast equality — never an epsilon
//     (an epsilon silently drops legitimate values near the sentinel).
//
// Every operator/kernel that needs "the invalid-value test for this band"
// resolves through here so the conventions cannot drift per file (#A1).
#pragma once

#include <cmath>
#include <limits>

class GdalDatasetWrapper;

namespace sicnu::rs
{

/// Declared NoData sentinel of @a band (1-based) cast to float; NaN when the
/// band declares no finite sentinel. O(1) GDAL metadata read.
float bandNoDataSentinel( const GdalDatasetWrapper &ds, int band );

/// True when @a v must be treated as missing: NaN (always invalid) or exact
/// equality with a declared, non-NaN @a sentinel.
inline bool isNoDataValue( float v, float sentinel )
{
    if ( std::isnan( v ) )
        return true;
    return !std::isnan( sentinel ) && v == sentinel;
}

} // namespace sicnu::rs
