// terrain_analysis.cpp — Phase 11.2
#include "terrain_analysis.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"

#include <cmath>
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

float TerrainAnalysis::getCell( const float *dem, int width, int height,
                                int row, int col, float nodata )
{
    if ( row < 0 || row >= height || col < 0 || col >= width )
        return nodata;
    return dem[static_cast<size_t>( row ) * width + col];
}

// ---------------------------------------------------------------------------
// Optimized slope — separate interior/border processing
// ---------------------------------------------------------------------------

bool TerrainAnalysis::slope( const float *dem, float *out, int width, int height,
                             float cellSize, float nodata )
{
    if ( !dem || !out ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "slope: null pointer argument");
        return false;
    }
    QString error;
    if ( !InputValidator::validateRasterDimensions( width, height, error ) ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return false;
    }
    if ( cellSize <= 0 ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("slope: invalid cellSize %1").arg(cellSize));
        return false;
    }

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Computing slope: %1x%2, cellSize=%3" )
        .arg( width ).arg( height ).arg( cellSize ) );

    const float invCs = 1.0f / cellSize;
    const float invCs2 = 1.0f / ( 2.0f * cellSize );
    const float inv8cs = 1.0f / ( 8.0f * cellSize );

    auto isInvalid = [nodata]( float v ) { return v == nodata || std::isnan( v ); };

    // Border pixels: use getCell with bounds checking
    auto processBorder = [&]( int r, int c ) {
        const size_t idx = static_cast<size_t>( r ) * width + c;
        const float z = dem[idx];
        if ( isInvalid( z ) ) { out[idx] = nodata; return; }

        const float a = getCell( dem, width, height, r - 1, c - 1, nodata );
        const float b = getCell( dem, width, height, r - 1, c, nodata );
        const float c2 = getCell( dem, width, height, r - 1, c + 1, nodata );
        const float d = getCell( dem, width, height, r, c - 1, nodata );
        const float f = getCell( dem, width, height, r, c + 1, nodata );
        const float g = getCell( dem, width, height, r + 1, c - 1, nodata );
        const float h = getCell( dem, width, height, r + 1, c, nodata );
        const float i = getCell( dem, width, height, r + 1, c + 1, nodata );

        bool hasNodata = ( isInvalid( a ) || isInvalid( b ) || isInvalid( c2 ) ||
                           isInvalid( d ) || isInvalid( f ) || isInvalid( g ) || isInvalid( h ) || isInvalid( i ) );
        float dzdx, dzdy;
        if ( hasNodata ) {
            const bool validF = !isInvalid( f );
            const bool validD = !isInvalid( d );
            if ( validF && validD ) {
                dzdx = ( f - d ) * invCs2;
            } else if ( validF ) {
                dzdx = ( f - z ) * invCs;
            } else if ( validD ) {
                dzdx = ( z - d ) * invCs;
            } else {
                dzdx = 0.0f;
            }

            const bool validH = !isInvalid( h );
            const bool validB = !isInvalid( b );
            if ( validH && validB ) {
                dzdy = ( h - b ) * invCs2;
            } else if ( validH ) {
                dzdy = ( h - z ) * invCs;
            } else if ( validB ) {
                dzdy = ( z - b ) * invCs;
            } else {
                dzdy = 0.0f;
            }
        } else {
            dzdx = ( ( c2 + 2.0f * f + i ) - ( a + 2.0f * d + g ) ) * inv8cs;
            dzdy = ( ( g + 2.0f * h + i ) - ( a + 2.0f * b + c2 ) ) * inv8cs;
        }
        out[idx] = std::atan( std::sqrt( dzdx * dzdx + dzdy * dzdy ) ) * 180.0f / static_cast<float>( M_PI );
    };

    // Interior pixels: direct array access, no bounds checks
    auto processInterior = [&]( int r, int c ) {
        const size_t idx = static_cast<size_t>( r ) * width + c;
        const float z = dem[idx];
        if ( isInvalid( z ) ) { out[idx] = nodata; return; }

        const float a = dem[( r - 1 ) * width + ( c - 1 )];
        const float b = dem[( r - 1 ) * width + c];
        const float c2 = dem[( r - 1 ) * width + ( c + 1 )];
        const float d = dem[r * width + ( c - 1 )];
        const float f = dem[r * width + ( c + 1 )];
        const float g = dem[( r + 1 ) * width + ( c - 1 )];
        const float h = dem[( r + 1 ) * width + c];
        const float i = dem[( r + 1 ) * width + ( c + 1 )];

        bool hasNodata = ( isInvalid( a ) || isInvalid( b ) || isInvalid( c2 ) ||
                           isInvalid( d ) || isInvalid( f ) || isInvalid( g ) || isInvalid( h ) || isInvalid( i ) );
        float dzdx, dzdy;
        if ( hasNodata ) {
            const bool validF = !isInvalid( f );
            const bool validD = !isInvalid( d );
            if ( validF && validD ) {
                dzdx = ( f - d ) * invCs2;
            } else if ( validF ) {
                dzdx = ( f - z ) * invCs;
            } else if ( validD ) {
                dzdx = ( z - d ) * invCs;
            } else {
                dzdx = 0.0f;
            }

            const bool validH = !isInvalid( h );
            const bool validB = !isInvalid( b );
            if ( validH && validB ) {
                dzdy = ( h - b ) * invCs2;
            } else if ( validH ) {
                dzdy = ( h - z ) * invCs;
            } else if ( validB ) {
                dzdy = ( z - b ) * invCs;
            } else {
                dzdy = 0.0f;
            }
        } else {
            dzdx = ( ( c2 + 2.0f * f + i ) - ( a + 2.0f * d + g ) ) * inv8cs;
            dzdy = ( ( g + 2.0f * h + i ) - ( a + 2.0f * b + c2 ) ) * inv8cs;
        }
        out[idx] = std::atan( std::sqrt( dzdx * dzdx + dzdy * dzdy ) ) * 180.0f / static_cast<float>( M_PI );
    };

    // First row
    for ( int c = 0; c < width; ++c ) processBorder( 0, c );
    // Interior rows
    for ( int r = 1; r < height - 1; ++r ) {
        processBorder( r, 0 );
        for ( int c = 1; c < width - 1; ++c ) processInterior( r, c );
        processBorder( r, width - 1 );
    }
    // Last row (if height > 1)
    if ( height > 1 )
        for ( int c = 0; c < width; ++c ) processBorder( height - 1, c );

    return true;
}

// ---------------------------------------------------------------------------
// Aspect (Horn 1981)
// ---------------------------------------------------------------------------

bool TerrainAnalysis::aspect( const float *dem, float *out, int width, int height,
                              float cellSize, float nodata )
{
    if ( !dem || !out ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "aspect: null pointer argument");
        return false;
    }
    QString error;
    if ( !InputValidator::validateRasterDimensions( width, height, error ) ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return false;
    }
    if ( cellSize <= 0 ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("aspect: invalid cellSize %1").arg(cellSize));
        return false;
    }

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Computing aspect: %1x%2" ).arg( width ).arg( height ) );

    const float invCs = 1.0f / cellSize;
    const float invCs2 = 1.0f / ( 2.0f * cellSize );
    const float inv8cs = 1.0f / ( 8.0f * cellSize );
    const float toDeg = 180.0f / static_cast<float>( M_PI );

    auto isInvalid = [nodata]( float v ) { return v == nodata || std::isnan( v ); };

    auto computeAspect = [&]( int r, int c, bool useBounds ) {
        const size_t idx = static_cast<size_t>( r ) * width + c;
        const float z = dem[idx];
        if ( isInvalid( z ) ) { out[idx] = nodata; return; }

        float a, b, c2, d, f, g, h, i;
        if ( useBounds ) {
            a = getCell( dem, width, height, r - 1, c - 1, nodata );
            b = getCell( dem, width, height, r - 1, c, nodata );
            c2 = getCell( dem, width, height, r - 1, c + 1, nodata );
            d = getCell( dem, width, height, r, c - 1, nodata );
            f = getCell( dem, width, height, r, c + 1, nodata );
            g = getCell( dem, width, height, r + 1, c - 1, nodata );
            h = getCell( dem, width, height, r + 1, c, nodata );
            i = getCell( dem, width, height, r + 1, c + 1, nodata );
        } else {
            a = dem[( r - 1 ) * width + ( c - 1 )];
            b = dem[( r - 1 ) * width + c];
            c2 = dem[( r - 1 ) * width + ( c + 1 )];
            d = dem[r * width + ( c - 1 )];
            f = dem[r * width + ( c + 1 )];
            g = dem[( r + 1 ) * width + ( c - 1 )];
            h = dem[( r + 1 ) * width + c];
            i = dem[( r + 1 ) * width + ( c + 1 )];
        }

        bool hasNodata = ( isInvalid( a ) || isInvalid( b ) || isInvalid( c2 ) ||
                           isInvalid( d ) || isInvalid( f ) || isInvalid( g ) || isInvalid( h ) || isInvalid( i ) );
        float dzdx, dzdy;
        if ( hasNodata ) {
            const bool validF = !isInvalid( f );
            const bool validD = !isInvalid( d );
            if ( validF && validD ) {
                dzdx = ( f - d ) * invCs2;
            } else if ( validF ) {
                dzdx = ( f - z ) * invCs;
            } else if ( validD ) {
                dzdx = ( z - d ) * invCs;
            } else {
                dzdx = 0.0f;
            }

            const bool validH = !isInvalid( h );
            const bool validB = !isInvalid( b );
            if ( validH && validB ) {
                dzdy = ( h - b ) * invCs2;
            } else if ( validH ) {
                dzdy = ( h - z ) * invCs;
            } else if ( validB ) {
                dzdy = ( z - b ) * invCs;
            } else {
                dzdy = 0.0f;
            }
        } else {
            dzdx = ( ( c2 + 2.0f * f + i ) - ( a + 2.0f * d + g ) ) * inv8cs;
            dzdy = ( ( g + 2.0f * h + i ) - ( a + 2.0f * b + c2 ) ) * inv8cs;
        }

        if ( std::abs( dzdx ) < 1e-10f && std::abs( dzdy ) < 1e-10f ) { out[idx] = -1.0f; return; }
        float angle = std::atan2( -dzdx, dzdy ) * toDeg;
        if ( angle < 0 ) angle += 360.0f;
        out[idx] = angle;
    };

    for ( int c = 0; c < width; ++c ) computeAspect( 0, c, true );
    for ( int r = 1; r < height - 1; ++r ) {
        computeAspect( r, 0, true );
        for ( int c = 1; c < width - 1; ++c ) computeAspect( r, c, false );
        computeAspect( r, width - 1, true );
    }
    if ( height > 1 )
        for ( int c = 0; c < width; ++c ) computeAspect( height - 1, c, true );

    return true;
}

// ---------------------------------------------------------------------------
// Hillshade
// ---------------------------------------------------------------------------

bool TerrainAnalysis::hillshade( const float *dem, float *out, int width, int height,
                                 float cellSize, float nodata,
                                 float sunAzimuth, float sunElevation )
{
    if ( !dem || !out ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "hillshade: null pointer argument");
        return false;
    }
    QString error;
    if ( !InputValidator::validateRasterDimensions( width, height, error ) ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return false;
    }
    if ( cellSize <= 0 ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("hillshade: invalid cellSize %1").arg(cellSize));
        return false;
    }

    SICNU_LOG_INFO( SicnuLogTags::Algorithms, QString( "Computing hillshade: %1x%2, azimuth=%3, elevation=%4" )
        .arg( width ).arg( height ).arg( sunAzimuth ).arg( sunElevation ) );

    const float zenRad = ( 90.0f - sunElevation ) * static_cast<float>( M_PI ) / 180.0f;
    const float azRad = sunAzimuth * static_cast<float>( M_PI ) / 180.0f;
    const float cosZen = std::cos( zenRad );
    const float sinZen = std::sin( zenRad );
    const float invCs = 1.0f / cellSize;
    const float invCs2 = 1.0f / ( 2.0f * cellSize );
    const float inv8cs = 1.0f / ( 8.0f * cellSize );

    auto isInvalid = [nodata]( float v ) { return v == nodata || std::isnan( v ); };
    auto computeHillshade = [&]( int r, int c, bool useBounds ) {
        const size_t idx = static_cast<size_t>( r ) * width + c;
        const float z = dem[idx];
        if ( isInvalid( z ) ) { out[idx] = nodata; return; }

        float a, b, c2, d, f, g, h, i;
        if ( useBounds ) {
            a = getCell( dem, width, height, r - 1, c - 1, nodata );
            b = getCell( dem, width, height, r - 1, c, nodata );
            c2 = getCell( dem, width, height, r - 1, c + 1, nodata );
            d = getCell( dem, width, height, r, c - 1, nodata );
            f = getCell( dem, width, height, r, c + 1, nodata );
            g = getCell( dem, width, height, r + 1, c - 1, nodata );
            h = getCell( dem, width, height, r + 1, c, nodata );
            i = getCell( dem, width, height, r + 1, c + 1, nodata );
        } else {
            a = dem[( r - 1 ) * width + ( c - 1 )];
            b = dem[( r - 1 ) * width + c];
            c2 = dem[( r - 1 ) * width + ( c + 1 )];
            d = dem[r * width + ( c - 1 )];
            f = dem[r * width + ( c + 1 )];
            g = dem[( r + 1 ) * width + ( c - 1 )];
            h = dem[( r + 1 ) * width + c];
            i = dem[( r + 1 ) * width + ( c + 1 )];
        }

        bool hasNodata = ( isInvalid( a ) || isInvalid( b ) || isInvalid( c2 ) ||
                           isInvalid( d ) || isInvalid( f ) || isInvalid( g ) || isInvalid( h ) || isInvalid( i ) );
        float dzdx, dzdy;
        if ( hasNodata ) {
            const bool validF = !isInvalid( f );
            const bool validD = !isInvalid( d );
            if ( validF && validD ) {
                dzdx = ( f - d ) * invCs2;
            } else if ( validF ) {
                dzdx = ( f - z ) * invCs;
            } else if ( validD ) {
                dzdx = ( z - d ) * invCs;
            } else {
                dzdx = 0.0f;
            }

            const bool validH = !isInvalid( h );
            const bool validB = !isInvalid( b );
            if ( validH && validB ) {
                dzdy = ( h - b ) * invCs2;
            } else if ( validH ) {
                dzdy = ( h - z ) * invCs;
            } else if ( validB ) {
                dzdy = ( z - b ) * invCs;
            } else {
                dzdy = 0.0f;
            }
        } else {
            dzdx = ( ( c2 + 2.0f * f + i ) - ( a + 2.0f * d + g ) ) * inv8cs;
            dzdy = ( ( g + 2.0f * h + i ) - ( a + 2.0f * b + c2 ) ) * inv8cs;
        }

        const float slopeRad = std::atan( std::sqrt( dzdx * dzdx + dzdy * dzdy ) );
        float aspectRad = ( std::abs( dzdx ) < 1e-10f && std::abs( dzdy ) < 1e-10f )
                          ? 0.0f : std::atan2( -dzdx, dzdy );
        if ( aspectRad < 0.0f ) aspectRad += static_cast<float>( 2.0 * M_PI );
        const float hs = cosZen * std::cos( slopeRad )
                         + sinZen * std::sin( slopeRad ) * std::cos( azRad - aspectRad );
        out[idx] = std::clamp( hs, 0.0f, 1.0f );
    };

    for ( int c = 0; c < width; ++c ) computeHillshade( 0, c, true );
    for ( int r = 1; r < height - 1; ++r ) {
        computeHillshade( r, 0, true );
        for ( int c = 1; c < width - 1; ++c ) computeHillshade( r, c, false );
        computeHillshade( r, width - 1, true );
    }
    if ( height > 1 )
        for ( int c = 0; c < width; ++c ) computeHillshade( height - 1, c, true );

    return true;
}

// ---------------------------------------------------------------------------
// Roughness (max-min in 3x3 window)
// ---------------------------------------------------------------------------

bool TerrainAnalysis::roughness( const float *dem, float *out, int width, int height,
                                 float nodata )
{
    if ( !dem || !out ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "roughness: null pointer argument");
        return false;
    }
    QString error;
    if ( !InputValidator::validateRasterDimensions( width, height, error ) ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return false;
    }

    for ( int r = 0; r < height; ++r )
    {
        for ( int c = 0; c < width; ++c )
        {
            const size_t idx = static_cast<size_t>( r ) * width + c;
            const float z = dem[idx];

            if ( z == nodata || std::isnan( z ) )
            {
                out[idx] = nodata;
                continue;
            }

            float zmin = z, zmax = z;
            for ( int dr = -1; dr <= 1; ++dr )
            {
                for ( int dc = -1; dc <= 1; ++dc )
                {
                    if ( dr == 0 && dc == 0 )
                        continue;
                    const float v = getCell( dem, width, height, r + dr, c + dc, nodata );
                    if ( v == nodata || std::isnan( v ) )
                        continue;
                    zmin = std::min( zmin, v );
                    zmax = std::max( zmax, v );
                }
            }
            out[idx] = zmax - zmin;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// TRI (Terrain Ruggedness Index)
// ---------------------------------------------------------------------------

bool TerrainAnalysis::tri( const float *dem, float *out, int width, int height,
                           float nodata )
{
    if ( !dem || !out ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "tri: null pointer argument");
        return false;
    }
    QString error;
    if ( !InputValidator::validateRasterDimensions( width, height, error ) ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return false;
    }

    for ( int r = 0; r < height; ++r )
    {
        for ( int c = 0; c < width; ++c )
        {
            const size_t idx = static_cast<size_t>( r ) * width + c;
            const float z = dem[idx];

            if ( z == nodata || std::isnan( z ) )
            {
                out[idx] = nodata;
                continue;
            }

            float sumDiff = 0;
            int count = 0;
            for ( int dr = -1; dr <= 1; ++dr )
            {
                for ( int dc = -1; dc <= 1; ++dc )
                {
                    if ( dr == 0 && dc == 0 )
                        continue;
                    const float v = getCell( dem, width, height, r + dr, c + dc, nodata );
                    if ( v == nodata || std::isnan( v ) )
                        continue;
                    sumDiff += std::abs( z - v );
                    count++;
                }
            }
            out[idx] = static_cast<float>(MathUtils::safeDivDouble(sumDiff, count));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// TPI (Topographic Position Index)
// ---------------------------------------------------------------------------

bool TerrainAnalysis::tpi( const float *dem, float *out, int width, int height,
                           float nodata )
{
    if ( !dem || !out ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "tpi: null pointer argument");
        return false;
    }
    QString error;
    if ( !InputValidator::validateRasterDimensions( width, height, error ) ) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, error);
        return false;
    }

    for ( int r = 0; r < height; ++r )
    {
        for ( int c = 0; c < width; ++c )
        {
            const size_t idx = static_cast<size_t>( r ) * width + c;
            const float z = dem[idx];

            if ( z == nodata || std::isnan( z ) )
            {
                out[idx] = nodata;
                continue;
            }

            float sum = 0;
            int count = 0;
            for ( int dr = -1; dr <= 1; ++dr )
            {
                for ( int dc = -1; dc <= 1; ++dc )
                {
                    if ( dr == 0 && dc == 0 )
                        continue;
                    const float v = getCell( dem, width, height, r + dr, c + dc, nodata );
                    if ( v == nodata || std::isnan( v ) )
                        continue;
                    sum += v;
                    count++;
                }
            }
            const float mean = count > 0 ? static_cast<float>(MathUtils::safeDivDouble(sum, count)) : z;
            out[idx] = z - mean;
        }
    }
    return true;
}
