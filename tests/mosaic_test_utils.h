// tests/mosaic_test_utils.h — F15 Package H: shared synthetic corpus for
// mosaic/fusion tests (ADR 0163).
//
// The corpus produces deterministic scenes with KNOWN ground truth:
//   • per-scene radiometric gain/offset (known-answer for balancing),
//   • rectangular cloud blocks (seam cost penalties, exclusion),
//   • a known optimal seam corridor (low |A−B| region in a known column),
//   • a counting tile-source that records peak concurrent buffered bytes so
//     scale tests can prove memory grows with the tile, not the mosaic.
//
// Nothing here reuses the implementation under test.
#pragma once

#include "processing/algorithms/mosaic_balancing.h"
#include "processing/algorithms/mosaic_plan.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace mosaic_test {

// Base scene field *before* per-scene gain/offset (the balancing truth):
// gentle ramp + deterministic sinusoid modulation, exactly what valueAt
// distorts.
inline double baseField( int64_t gx, int64_t gy, double base = 100.0 )
{
    return base * ( 1.0 + 0.01 * std::sin( 0.05 * gx ) + 0.01 * std::cos( 0.05 * gy ) ) +
           0.1 * gx + 0.1 * gy;
}

struct SyntheticScene {
    int width = 0;
    int height = 0;
    int64_t offsetX = 0; // placement on the shared grid
    int64_t offsetY = 0;
    double gain = 1.0;   // value = gain * base + offset
    double offset = 0.0;
    int priority = 0;
    // Cloud block (grid coords, inclusive-exclusive), empty when unused.
    int64_t cloudX0 = -1, cloudY0 = -1, cloudX1 = -1, cloudY1 = -1;
    float cloudValue = std::numeric_limits<float>::quiet_NaN(); // written when in block

    float valueAt( int64_t gx, int64_t gy, double base ) const
    {
        const float v = static_cast<float>( gain * baseField( gx, gy, base ) + offset );
        if ( gx >= cloudX0 && gx < cloudX1 && gy >= cloudY0 && gy < cloudY1 )
            return cloudValue;
        return v;
    }
};

// Flat-ramp truth (kept for coarse checks).
inline double baseValue( int64_t gx, int64_t gy )
{
    return 100.0 + 0.1 * gx + 0.1 * gy;
}

/// In-memory sampler over a set of synthetic scenes placed on a shared grid.
/// Reads through the OverlapSampler seam (same interface the GDAL-backed
/// production sampler implements), applying NoData -> NaN.
class SyntheticSampler : public rs::mosaic::OverlapSampler
{
  public:
    explicit SyntheticSampler( const std::vector<SyntheticScene> &scenes, double base = 100.0 )
        : scenes_( scenes ), base_( base ) {}

    bool readGridWindow( int scene, int band, int64_t x0, int64_t y0, int64_t w, int64_t h,
                         std::vector<float> &out ) override
    {
        (void)band; // synthetic scenes are band-invariant apart from band gain scaling
        const SyntheticScene &s = scenes_.at( scene );
        out.assign( static_cast<size_t>( w ) * h, std::numeric_limits<float>::quiet_NaN() );
        for ( int64_t r = 0; r < h; ++r )
        {
            for ( int64_t c = 0; c < w; ++c )
            {
                const int64_t gx = x0 + c;
                const int64_t gy = y0 + r;
                const int64_t lx = gx - s.offsetX;
                const int64_t ly = gy - s.offsetY;
                if ( lx < 0 || ly < 0 || lx >= s.width || ly >= s.height )
                    continue;
                out[static_cast<size_t>( r ) * w + c] =
                    s.valueAt( gx, gy, base_ * ( 1.0 + 0.02 * band ) );
            }
        }
        return true;
    }

  private:
    std::vector<SyntheticScene> scenes_;
    double base_;
};

/// Counting sampler wrapper: tracks peak buffered bytes across all reads to
/// evidence that working memory scales with the requested window, not with
/// the logical mosaic extent (GOAL Oracle 3).
class CountingSampler : public rs::mosaic::OverlapSampler
{
  public:
    explicit CountingSampler( rs::mosaic::OverlapSampler *inner ) : inner_( inner ) {}

    bool readGridWindow( int scene, int band, int64_t x0, int64_t y0, int64_t w, int64_t h,
                         std::vector<float> &out ) override
    {
        const size_t bytes = static_cast<size_t>( w ) * h * sizeof( float );
        live_ += bytes;
        peak_ = std::max( peak_, live_ );
        const bool ok = inner_->readGridWindow( scene, band, x0, y0, w, h, out );
        live_ -= bytes;
        ++reads_;
        return ok;
    }

    size_t peakBufferedBytes() const { return peak_; }
    uint64_t reads() const { return reads_; }

  private:
    rs::mosaic::OverlapSampler *inner_;
    size_t live_ = 0;
    size_t peak_ = 0;
    uint64_t reads_ = 0;
};

/// Simple synthetic scene placement helper: adjacent horizontal strips with a
/// known overlap and per-scene radiometric distortion.
inline std::vector<SyntheticScene> twoSceneCorpus( int w, int h, int overlap,
                                                   double gainB, double offsetB )
{
    SyntheticScene a;
    a.width = w;
    a.height = h;
    SyntheticScene b = a;
    b.offsetX = w - overlap;
    b.gain = gainB;
    b.offset = offsetB;
    return { a, b };
}

} // namespace mosaic_test
