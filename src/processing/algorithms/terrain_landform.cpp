// terrain_landform.cpp — see terrain_landform.h for contracts.

#include "terrain_landform.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "terrain_analysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace TerrainLandform
{
namespace
{

inline bool isMissing( float z, float nodata )
{
    return z == nodata || std::isnan( z );
}

constexpr double kDegToRad = M_PI / 180.0;

struct Compass8
{
    int dx;
    int dy;
};

// Fixed direction order N, NE, E, SE, S, SW, W, NW (index = base-3 digit).
constexpr Compass8 kDirs[8] = {
    { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 },
};

} // namespace

bool tpiMultiscale( const float *dem, int width, int height, float nodata,
                    const std::vector<int> &radiiCells,
                    std::vector<MultiscaleTPI> *out,
                    const std::function<bool()> &cancelled )
{
    if ( !dem || !out || width <= 0 || height <= 0 )
        return false;
    if ( cancelled && cancelled() )
        return false;
    for ( const int r : radiiCells )
        if ( r <= 0 )
            return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;

    out->clear();
    out->reserve( radiiCells.size() );
    for ( const int r : radiiCells )
    {
        MultiscaleTPI scale;
        scale.radiusCells = r;
        scale.tpi.assign( n, 0.0f );
        scale.stdTpi.assign( n, 0.0f );
        out->push_back( std::move( scale ) );
    }

    // Integral images of z, z² and the valid mask make every window sum an
    // O(1) lookup; one pass per scale.
    for ( std::size_t s = 0; s < radiiCells.size(); ++s )
    {
        const int r = radiiCells[s];
        const std::size_t stride = static_cast<std::size_t>( width ) + 1;
        std::vector<double> sumZ( stride * ( static_cast<std::size_t>( height ) + 1 ), 0.0 );
        std::vector<double> sumZ2( stride * ( static_cast<std::size_t>( height ) + 1 ), 0.0 );
        std::vector<double> sumN( stride * ( static_cast<std::size_t>( height ) + 1 ), 0.0 );
        for ( int y = 0; y < height; ++y )
            for ( int x = 0; x < width; ++x )
            {
                const std::size_t cell = static_cast<std::size_t>( y ) * width + x;
                const double v = isMissing( dem[cell], nodata )
                                     ? 0.0
                                     : static_cast<double>( dem[cell] );
                const double valid = isMissing( dem[cell], nodata ) ? 0.0 : 1.0;
                const std::size_t here = ( static_cast<std::size_t>( y ) + 1 ) * stride
                                         + ( static_cast<std::size_t>( x ) + 1 );
                sumZ[here] = v + sumZ[here - 1] + sumZ[here - stride] - sumZ[here - stride - 1];
                sumZ2[here] = v * v + sumZ2[here - 1] + sumZ2[here - stride]
                              - sumZ2[here - stride - 1];
                sumN[here] = valid + sumN[here - 1] + sumN[here - stride]
                             - sumN[here - stride - 1];
            }

        auto windowSum = [&]( const std::vector<double> &ii, int x0, int y0, int x1,
                              int y1 ) {
            const std::size_t stride2 = stride;
            return ii[( static_cast<std::size_t>( y1 ) + 1 ) * stride2
                      + ( static_cast<std::size_t>( x1 ) + 1 )]
                   - ii[( static_cast<std::size_t>( y0 ) ) * stride2
                        + ( static_cast<std::size_t>( x1 ) + 1 )]
                   - ii[( static_cast<std::size_t>( y1 ) + 1 ) * stride2
                        + ( static_cast<std::size_t>( x0 ) )]
                   + ii[( static_cast<std::size_t>( y0 ) ) * stride2
                        + ( static_cast<std::size_t>( x0 ) )];
        };

        for ( int y = 0; y < height; ++y )
        {
            for ( int x = 0; x < width; ++x )
            {
                const std::size_t i = static_cast<std::size_t>( y ) * width + x;
                float &tpi = ( *out )[s].tpi[i];
                float &stdTpi = ( *out )[s].stdTpi[i];
                if ( isMissing( dem[i], nodata ) )
                {
                    tpi = nodata;
                    stdTpi = nodata;
                    continue;
                }
                const int x0 = std::max( 0, x - r );
                const int y0 = std::max( 0, y - r );
                const int x1 = std::min( width - 1, x + r );
                const int y1 = std::min( height - 1, y + r );
                const double count = windowSum( sumN, x0, y0, x1, y1 ) - 1.0;
                if ( count < 1.0 )
                    continue; // no valid neighbour: TPI stays 0
                const double sum = windowSum( sumZ, x0, y0, x1, y1 )
                                   - static_cast<double>( dem[i] );
                const double sumSq = windowSum( sumZ2, x0, y0, x1, y1 )
                                     - static_cast<double>( dem[i] ) * dem[i];
                const double mean = sum / count;
                tpi = static_cast<float>( static_cast<double>( dem[i] ) - mean );
                const double variance =
                    std::max( 0.0, sumSq / count - mean * mean );
                const double sd = std::sqrt( variance );
                stdTpi = sd > 0.0 ? static_cast<float>( tpi / sd ) : 0.0f;
            }
        }
        if ( cancelled && ( ( s & 0x03 ) == 0 ) && cancelled() )
            return false;
    }
    return true;
}

bool landformClasses( const float *dem, int width, int height, float nodata,
                      double cellSizeX, double cellSizeY,
                      const LandformClassParams &params,
                      std::vector<std::uint8_t> *classes,
                      const std::function<bool()> &cancelled )
{
    if ( !dem || !classes || width <= 0 || height <= 0 || cellSizeX <= 0.0
         || cellSizeY <= 0.0 )
        return false;
    if ( cancelled && cancelled() )
        return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;

    // Slope via the existing family kernel (single authority for Horn slope).
    std::vector<float> slopeDeg( n, 0.0f );
    if ( !TerrainAnalysis::slope( dem, slopeDeg.data(), width, height,
                                  static_cast<float>( cellSizeX ),
                                  static_cast<float>( cellSizeY ), nodata ) )
        return false;

    std::vector<MultiscaleTPI> scales;
    if ( !tpiMultiscale( dem, width, height, nodata,
                         { params.innerRadiusCells, params.outerRadiusCells }, &scales,
                         cancelled ) )
        return false;
    const MultiscaleTPI &inner = scales[0];
    const MultiscaleTPI &outer = scales[1];

    classes->assign( n, 0 );
    for ( std::size_t i = 0; i < n; ++i )
    {
        if ( isMissing( dem[i], nodata ) )
        {
            ( *classes )[i] = 255; // NoData marker in the byte class grid
            continue;
        }
        if ( slopeDeg[i] < params.flatSlopeDeg )
        {
            ( *classes )[i] = 0; // plains
            continue;
        }
        const double m = outer.stdTpi[i];
        const double s = inner.stdTpi[i];
        std::uint8_t code;
        if ( m > 1.0 )
            code = s > 1.0 ? 5 : 4; // peak : upper slope
        else if ( m >= -1.0 )
            code = s > 1.0 ? 4 : ( s < -1.0 ? 2 : 3 ); // upper : lower : middle
        else
            code = s < -1.0 ? 1 : 2; // valley : lower slope
        ( *classes )[i] = code;
    }
    return true;
}

bool geomorphon( const float *dem, int width, int height, float nodata,
                 double cellSizeX, double cellSizeY, const GeomorphonParams &params,
                 GeomorphonResult *out,
                 const std::function<bool()> &cancelled )
{
    if ( !dem || !out || width <= 0 || height <= 0 || cellSizeX <= 0.0
         || cellSizeY <= 0.0 || params.searchRadiusCells <= 0
         || params.flatRadiusCells < 0 || params.flatThreshDeg < 0.0 )
        return false;
    if ( cancelled && cancelled() )
        return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;
    out->pattern.assign( n, 0 );
    out->form.assign( n, 0 );

    // Base-3 digit weights for the fixed direction order.
    constexpr std::uint16_t kPow3[8] = { 1, 3, 9, 27, 81, 243, 729, 2187 };
    constexpr std::uint16_t kPatternNoData = 65535;

    const double tanFlat = std::tan( params.flatThreshDeg * kDegToRad );
    const double minCell = std::min( cellSizeX, cellSizeY );
    const double startDist = ( static_cast<double>( params.flatRadiusCells ) + 1.0 )
                             * minCell;
    const double maxDist = static_cast<double>( params.searchRadiusCells ) * minCell;

    std::vector<int> code( 8, 0 );
    for ( int y = 0; y < height; ++y )
    {
        for ( int x = 0; x < width; ++x )
        {
            const std::size_t i = static_cast<std::size_t>( y ) * width + x;
            if ( isMissing( dem[i], nodata ) )
            {
                out->pattern[i] = kPatternNoData;
                out->form[i] = 255;
                continue;
            }
            const double zc = dem[i];

            int pos = 0;
            int neg = 0;
            for ( int d = 0; d < 8; ++d )
            {
                const double azRad = d * 45.0 * kDegToRad;
                const double ux = std::sin( azRad );
                const double uy = -std::cos( azRad );
                double zenithTan = -std::numeric_limits<double>::infinity();
                double nadirTan = std::numeric_limits<double>::infinity();
                // Nearest-cell march with visited guard, like the horizon
                // profile kernel (same approximation class, documented).
                std::uint32_t last = std::numeric_limits<std::uint32_t>::max();
                const double step = minCell * 0.5;
                for ( double t = startDist; t <= maxDist; t += step )
                {
                    const int cx =
                        static_cast<int>( std::floor( x + 0.5 + t * ux / cellSizeX ) );
                    const int cy =
                        static_cast<int>( std::floor( y + 0.5 + t * uy / cellSizeY ) );
                    if ( cx < 0 || cy < 0 || cx >= width || cy >= height )
                        break;
                    const std::uint32_t cell =
                        static_cast<std::uint32_t>( cy ) * width + cx;
                    if ( cell == last )
                        continue;
                    last = cell;
                    if ( isMissing( dem[cell], nodata ) )
                        break; // NoData ends the line of sight
                    // Angle over the exact centre-to-centre span (the march
                    // t only selects the sampled cell).
                    const int ccx = static_cast<int>( cell % width );
                    const int ccy = static_cast<int>( cell / width );
                    const double dc = std::hypot( ( ccx - x ) * cellSizeX,
                                                  ( ccy - y ) * cellSizeY );
                    const double tanAngle =
                        ( static_cast<double>( dem[cell] ) - zc ) / dc;
                    zenithTan = std::max( zenithTan, tanAngle );
                    nadirTan = std::min( nadirTan, tanAngle );
                }
                int c = 0;
                if ( zenithTan > tanFlat && zenithTan >= -nadirTan )
                    c = 1;
                else if ( nadirTan < -tanFlat && -nadirTan > zenithTan )
                    c = -1;
                code[d] = c;
                if ( c > 0 )
                    ++pos;
                else if ( c < 0 )
                    ++neg;
                // Base-3 packing: digit = code + 1 ∈ {0,1,2}.
                out->pattern[i] =
                    static_cast<std::uint16_t>( out->pattern[i]
                                                + static_cast<std::uint16_t>( c + 1 )
                                                    * kPow3[d] );
            }

            // Form class from the ternary pattern (header table).
            std::uint8_t form;
            if ( pos == 0 && neg == 0 )
                form = 0; // flat
            else if ( neg == 8 )
                form = 1; // peak
            else if ( pos == 8 )
                form = 2; // pit
            else
            {
                // Count circular arcs of constant sign over the non-zero
                // codes (zeros act as separators).
                int plusArcs = 0;
                int minusArcs = 0;
                for ( int d = 0; d < 8; ++d )
                {
                    const int cur = code[d];
                    const int prev = code[( d + 7 ) & 7];
                    if ( cur > 0 && prev <= 0 )
                        ++plusArcs;
                    if ( cur < 0 && prev >= 0 )
                        ++minusArcs;
                }
                if ( plusArcs == 0 && neg > 0 )
                    form = 3; // ridge: one or more − arcs, no +
                else if ( minusArcs == 0 && pos > 0 )
                    form = 4; // valley
                else if ( plusArcs == 1 && minusArcs == 1 )
                    form = 5; // slope
                else
                    form = 6; // other
            }
            out->form[i] = form;
        }
        if ( cancelled && ( ( y & 0x3F ) == 0 ) && cancelled() )
            return false;
    }
    return true;
}

} // namespace TerrainLandform
