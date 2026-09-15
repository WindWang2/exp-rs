// mosaic_blend.cpp — F15 Package D implementation (ADR 0163).
#include "mosaic_blend.h"

#include <algorithm>
#include <cmath>

namespace rs::mosaic {

double featherWeight( double signedDistancePx, int featherWidth )
{
    const double w = std::max( 1, featherWidth );
    return std::clamp( 0.5 + signedDistancePx / ( 2.0 * w ), 0.0, 1.0 );
}

float blendValue( float a, float b, double s )
{
    const bool aBad = !std::isfinite( a );
    const bool bBad = !std::isfinite( b );
    if ( aBad && bBad )
        return std::numeric_limits<float>::quiet_NaN();
    if ( aBad )
        return b;
    if ( bBad )
        return a;
    return static_cast<float>( ( 1.0 - s ) * a + s * b );
}

namespace {

// 2x mean downsample (even/odd edges handled by averaging whatever exists).
std::vector<float> downsample2( const std::vector<float> &src, int w, int h, int *outW, int *outH )
{
    const int nw = std::max( 1, w / 2 );
    const int nh = std::max( 1, h / 2 );
    std::vector<float> dst( static_cast<size_t>( nw ) * nh );
    for ( int r = 0; r < nh; ++r )
    {
        for ( int c = 0; c < nw; ++c )
        {
            const int r0 = std::min( 2 * r, h - 1 );
            const int r1 = std::min( 2 * r + 1, h - 1 );
            const int c0 = std::min( 2 * c, w - 1 );
            const int c1 = std::min( 2 * c + 1, w - 1 );
            const double sum = src[static_cast<size_t>( r0 ) * w + c0] +
                               src[static_cast<size_t>( r1 ) * w + c0] +
                               src[static_cast<size_t>( r0 ) * w + c1] +
                               src[static_cast<size_t>( r1 ) * w + c1];
            dst[static_cast<size_t>( r ) * nw + c] = static_cast<float>( sum / 4.0 );
        }
    }
    *outW = nw;
    *outH = nh;
    return dst;
}

// 2x nearest-replicate upsample: exact inverse of the averaging for constant
// inputs and the telescoping identity for pyramid reconstruction.
std::vector<float> upsample2( const std::vector<float> &src, int w, int h, int outW, int outH )
{
    std::vector<float> dst( static_cast<size_t>( outW ) * outH );
    for ( int r = 0; r < outH; ++r )
    {
        const int sr = std::min( r / 2, h - 1 );
        for ( int c = 0; c < outW; ++c )
        {
            const int sc = std::min( c / 2, w - 1 );
            dst[static_cast<size_t>( r ) * outW + c] = src[static_cast<size_t>( sr ) * w + sc];
        }
    }
    return dst;
}

std::vector<double> downsample2Weights( const std::vector<double> &src, int w, int h,
                                        int *outW, int *outH )
{
    const int nw = std::max( 1, w / 2 );
    const int nh = std::max( 1, h / 2 );
    std::vector<double> dst( static_cast<size_t>( nw ) * nh );
    for ( int r = 0; r < nh; ++r )
    {
        for ( int c = 0; c < nw; ++c )
        {
            const int r0 = std::min( 2 * r, h - 1 );
            const int r1 = std::min( 2 * r + 1, h - 1 );
            const int c0 = std::min( 2 * c, w - 1 );
            const int c1 = std::min( 2 * c + 1, w - 1 );
            dst[static_cast<size_t>( r ) * nw + c] =
                0.25 * ( src[static_cast<size_t>( r0 ) * w + c0] +
                         src[static_cast<size_t>( r1 ) * w + c0] +
                         src[static_cast<size_t>( r0 ) * w + c1] +
                         src[static_cast<size_t>( r1 ) * w + c1] );
        }
    }
    *outW = nw;
    *outH = nh;
    return dst;
}

} // namespace

bool blendMultibandWindow( const std::vector<float> &a, const std::vector<float> &b,
                           const std::vector<double> &wB, int width, int height, int levels,
                           std::vector<float> *out )
{
    if ( !out || width <= 0 || height <= 0 || levels < 1 )
        return false;
    if ( a.size() != b.size() || wB.size() != a.size() ||
         a.size() != static_cast<size_t>( width ) * height )
        return false;

    // Build Gaussian/Laplacian pyramids. NaN pixels are treated as 0 in the
    // pyramid and masked out of the final blend by the caller-side weight
    // semantics of blendValue (the operator blends blended windows back with
    // blendValue for NoData fallback).
    struct Level
    {
        std::vector<float> lap; // a-side Laplacian
        std::vector<float> bLap;
        int w = 0, h = 0;
    };
    std::vector<Level> pyramid;
    pyramid.reserve( levels );

    std::vector<float> ga( a ), gb( b );
    int w = width, h = height;
    for ( int l = 0; l < levels - 1; ++l )
    {
        int nw = 0, nh = 0;
        std::vector<float> dga = downsample2( ga, w, h, &nw, &nh );
        std::vector<float> dgb = downsample2( gb, w, h, &nw, &nh );
        std::vector<float> upa = upsample2( dga, nw, nh, w, h );
        std::vector<float> upb = upsample2( dgb, nw, nh, w, h );
        Level lvl;
        lvl.w = w;
        lvl.h = h;
        lvl.lap.resize( ga.size() );
        lvl.bLap.resize( gb.size() );
        for ( size_t i = 0; i < ga.size(); ++i )
        {
            lvl.lap[i] = std::isfinite( ga[i] ) ? ga[i] - upa[i] : 0.0f;
            lvl.bLap[i] = std::isfinite( gb[i] ) ? gb[i] - upb[i] : 0.0f;
        }
        pyramid.push_back( std::move( lvl ) );
        ga = std::move( dga );
        gb = std::move( dgb );
        w = nw;
        h = nh;
    }

    // Per-level weight maps (level 0 = full resolution; dims track ga/gb).
    std::vector<std::vector<double>> wLevels;
    wLevels.reserve( levels );
    {
        std::vector<double> cur = wB;
        int cw = width, ch = height;
        for ( int l = 0; l < levels; ++l )
        {
            wLevels.push_back( cur );
            if ( l + 1 < levels )
            {
                int nw2 = 0, nh2 = 0;
                cur = downsample2Weights( cur, cw, ch, &nw2, &nh2 );
                cw = nw2;
                ch = nh2;
            }
        }
    }

    // Coarsest level: blend the residual Gaussians with the coarsest weights.
    // (w/h currently hold the coarsest dims from the pyramid build loop.)
    std::vector<float> acc( ga.size() );
    const std::vector<double> &wTop = wLevels.back();
    for ( size_t i = 0; i < acc.size(); ++i )
        acc[i] = static_cast<float>( wTop[i] * gb[i] + ( 1.0 - wTop[i] ) * ga[i] );

    for ( int l = static_cast<int>( pyramid.size() ) - 1; l >= 0; --l )
    {
        const Level &lvl = pyramid[l];
        const std::vector<float> up = upsample2( acc, w, h, lvl.w, lvl.h );
        const std::vector<double> &wl = wLevels[l];
        acc.assign( static_cast<size_t>( lvl.w ) * lvl.h, 0.0f );
        for ( size_t i = 0; i < acc.size(); ++i )
        {
            acc[i] = static_cast<float>( wl[i] * ( lvl.bLap[i] + up[i] ) +
                                         ( 1.0 - wl[i] ) * ( lvl.lap[i] + up[i] ) );
        }
        w = lvl.w;
        h = lvl.h;
    }

    // The upsampled Laplacian path can carry small negative overshoot; clamp
    // to the convex hull of the two inputs per pixel to keep the blend
    // artifact-free (halo guard).
    acc.resize( a.size() );
    for ( size_t i = 0; i < acc.size(); ++i )
    {
        const double s = wB[i];
        const double lo = std::min( static_cast<double>( a[i] ), static_cast<double>( b[i] ) );
        const double hi = std::max( static_cast<double>( a[i] ), static_cast<double>( b[i] ) );
        const double target = s * b[i] + ( 1.0 - s ) * a[i];
        double v = acc[i];
        if ( !std::isfinite( v ) )
            v = target;
        v = std::clamp( v, std::min( lo, target ), std::max( hi, target ) );
        acc[i] = static_cast<float>( v );
    }
    *out = std::move( acc );
    return true;
}

} // namespace rs::mosaic
