// spectral_spatial_fusion.cpp — see spectral_spatial_fusion.h for contracts.

#include "spectral_spatial_fusion.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SpectralSpatialFusion
{
bool fuseScores( const float *scores, const uint8_t *valid, int width, int height,
                 const Config &config, Result *result, QString *errorMessage )
{
    if ( !scores || !result || width <= 0 || height <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid fusion arguments" );
        return false;
    }
    if ( config.radius < 0 || !std::isfinite( config.beta ) ||
         config.beta < 0.0 || config.beta > 1.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "Invalid fusion config: radius must be >= 0 and beta within [0, 1]" );
        return false;
    }

    const size_t plane = static_cast<size_t>( width ) * height;
    result->fused.assign( plane, 0.0f );
    result->covered.assign( plane, 0 );
    result->neighborCount.assign( plane, 0 );

    const int r = config.radius;

    for ( int y = 0; y < height; ++y )
    {
        for ( int x = 0; x < width; ++x )
        {
            const size_t p = static_cast<size_t>( y ) * width + x;
            const float self = scores[p];
            const bool selfValid =
                valid ? ( valid[p] != 0 ) : std::isfinite( self );
            if ( !selfValid )
            {
                result->fused[p] = std::numeric_limits<float>::quiet_NaN();
                continue;
            }

            // Mean over valid window members (clamped to the raster). The
            // self pixel is a member, so the mean is defined for every valid
            // pixel and NoData never enters the sum.
            const int y0 = std::max( 0, y - r );
            const int y1 = std::min( height - 1, y + r );
            const int x0 = std::max( 0, x - r );
            const int x1 = std::min( width - 1, x + r );
            double sum = 0.0;
            int32_t count = 0;
            for ( int wy = y0; wy <= y1; ++wy )
            {
                for ( int wx = x0; wx <= x1; ++wx )
                {
                    const size_t q = static_cast<size_t>( wy ) * width + wx;
                    const float v = scores[q];
                    const bool qValid = valid ? ( valid[q] != 0 ) : std::isfinite( v );
                    if ( !qValid )
                        continue;
                    sum += static_cast<double>( v );
                    ++count;
                }
            }
            result->neighborCount[p] = count;

            const double mean = sum / static_cast<double>( count );
            const double fused =
                ( 1.0 - config.beta ) * static_cast<double>( self ) + config.beta * mean;
            result->fused[p] = static_cast<float>( fused );
            result->covered[p] = 1;
        }
    }
    return true;
}
} // namespace SpectralSpatialFusion
