/***************************************************************************
  processing/algorithms/spatiotemporal_filter.cpp
  Temporal Phenology Timeline Studio (D16) — STARFM kernel implementation.
  ---------------------------
  See spatiotemporal_filter.h for the seam contract (ADR 0161).
 ***************************************************************************/

#include "processing/algorithms/spatiotemporal_filter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::temporal
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
// Distance epsilon (D16 §F): keeps the composite distance strictly positive
// for pixel-identical candidates.
constexpr double kEpsilonS = 1e-3;
constexpr double kEpsilonT = 1e-3;
} // namespace

std::vector<float> SpatiotemporalFilter::predictStarfm( const float *fine0,
                                                        const float *coarse0,
                                                        const float *coarseK, int width,
                                                        int height,
                                                        const StarfmOptions &options )
{
    if ( !fine0 || !coarse0 || !coarseK || width < 1 || height < 1 )
        return {};

    const std::size_t pixels = static_cast<std::size_t>( width ) * height;
    const int radius = std::max( 0, options.windowRadius );
    const double threshold = options.spectralThreshold;
    const double decay = options.spatialWeightDecay > 0.0f
                             ? static_cast<double>( options.spatialWeightDecay )
                             : 1.0;

    std::vector<float> out( pixels, kNan );
    for ( int cy = 0; cy < height; ++cy )
    {
        for ( int cx = 0; cx < width; ++cx )
        {
            const std::size_t center = static_cast<std::size_t>( cy ) * width + cx;
            const double fc = fine0[center];
            if ( !std::isfinite( fc ) )
                continue; // output stays NaN

            const int y0 = std::max( 0, cy - radius );
            const int y1 = std::min( height - 1, cy + radius );
            const int x0 = std::max( 0, cx - radius );
            const int x1 = std::min( width - 1, cx + radius );

            double sumWeight = 0.0;
            double sumWeighted = 0.0;
            double sumDelta = 0.0;
            int deltaCount = 0;

            for ( int yy = y0; yy <= y1; ++yy )
            {
                for ( int xx = x0; xx <= x1; ++xx )
                {
                    const std::size_t i = static_cast<std::size_t>( yy ) * width + xx;
                    const double f = fine0[i];
                    const double c0 = coarse0[i];
                    const double ck = coarseK[i];
                    if ( !std::isfinite( f ) || !std::isfinite( c0 ) || !std::isfinite( ck ) )
                        continue; // NaN candidates never contribute

                    const double coarseDelta = ck - c0;
                    if ( std::abs( f - fc ) <= threshold )
                    {
                        // Homogeneity gate: heterogeneous pixels do not leak
                        // across the edge.
                        const double s = std::abs( f - c0 ) + kEpsilonS;
                        const double tDiff = std::abs( ck - c0 ) + kEpsilonT;
                        const double dDist =
                            ( std::sqrt( static_cast<double>( ( yy - cy ) * ( yy - cy ) +
                                                           ( xx - cx ) * ( xx - cx ) ) ) +
                              1.0 ) *
                            decay;
                        const double weight = 1.0 / ( s * tDiff * dDist );
                        sumWeight += weight;
                        sumWeighted += weight * ( ck + f - c0 );
                    }
                    // The fallback level accumulates coarse deltas over ALL
                    // valid window cells (gate excluded — header contract:
                    // "the center pixel's base value carries the coarse
                    // temporal change").
                    sumDelta += coarseDelta;
                    ++deltaCount;
                }
            }

            double predicted;
            if ( sumWeight > 1e-12 )
                predicted = sumWeighted / sumWeight;
            else if ( deltaCount > 0 )
                predicted = fc + sumDelta / deltaCount; // documented fallback
            else
                predicted = kNan; // nothing valid anywhere: undefined
            if ( std::isfinite( predicted ) )
                out[center] = std::min( 1.0f, std::max( 0.0f, static_cast<float>( predicted ) ) );
        }
    }
    return out;
}

} // namespace sicnu::temporal
