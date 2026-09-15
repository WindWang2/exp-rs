// tests/test_mosaic_blend.cpp — F15 Package D oracle tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/mosaic_blend.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace rs::mosaic;
using Catch::Approx;

TEST_CASE( "Feather: ramp is symmetric, clamped and centered",
           "[processing][mosaic][blend]" )
{
    CHECK( featherWeight( 0.0, 32 ) == Approx( 0.5 ) );
    CHECK( featherWeight( 32.0, 32 ) == Approx( 1.0 ) );
    CHECK( featherWeight( -32.0, 32 ) == Approx( 0.0 ) );
    CHECK( featherWeight( 1000.0, 32 ) == Approx( 1.0 ) );
    CHECK( featherWeight( -1000.0, 32 ) == Approx( 0.0 ) );
    CHECK( featherWeight( 8.0, 32 ) == Approx( 0.5 + 8.0 / 64.0 ) );
    CHECK( featherWeight( -8.0, 32 ) == Approx( 0.5 - 8.0 / 64.0 ) );
}

TEST_CASE( "Feather: weights of both sides always sum to one",
           "[processing][mosaic][blend]" )
{
    for ( int d = -40; d <= 40; ++d )
    {
        const double s = featherWeight( static_cast<double>( d ), 16 );
        CHECK( ( s + ( 1.0 - s ) ) == Approx( 1.0 ) );
        CHECK( s >= 0.0 );
        CHECK( s <= 1.0 );
    }
}

TEST_CASE( "Blend value: NoData fallback prevents cracks",
           "[processing][mosaic][blend]" )
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK( blendValue( 10.0f, 20.0f, 0.25 ) == Approx( 12.5f ) );
    CHECK( blendValue( nan, 20.0f, 0.0 ) == 20.0f ); // A invalid -> B regardless of s
    CHECK( blendValue( 10.0f, nan, 1.0 ) == 10.0f ); // B invalid -> A
    CHECK( std::isnan( blendValue( nan, nan, 0.5 ) ) );
}

TEST_CASE( "Multiband: constant inputs reconstruct the weighted value exactly",
           "[processing][mosaic][blend]" )
{
    const int w = 33, h = 17; // odd sizes exercise the downsample edge rules
    std::vector<float> a( static_cast<size_t>( w ) * h, 10.0f );
    std::vector<float> b( static_cast<size_t>( w ) * h, 20.0f );
    std::vector<double> wB( static_cast<size_t>( w ) * h, 0.25 );

    for ( int levels = 1; levels <= 4; ++levels )
    {
        std::vector<float> out;
        REQUIRE( blendMultibandWindow( a, b, wB, w, h, levels, &out ) );
        REQUIRE( out.size() == a.size() );
        for ( size_t i = 0; i < out.size(); ++i )
            CHECK( out[i] == Approx( 0.75 * 10.0 + 0.25 * 20.0 ).margin( 1e-3 ) );
    }
}

TEST_CASE( "Multiband: weight extremes reproduce each side exactly",
           "[processing][mosaic][blend]" )
{
    const int w = 16, h = 16;
    std::vector<float> a( static_cast<size_t>( w ) * h );
    std::vector<float> b( static_cast<size_t>( w ) * h );
    for ( size_t i = 0; i < a.size(); ++i )
    {
        a[i] = static_cast<float>( i % 7 ) + 3.0f;
        b[i] = 50.0f - static_cast<float>( i % 5 );
    }
    std::vector<double> one( a.size(), 1.0 );
    std::vector<double> zero( a.size(), 0.0 );

    std::vector<float> out;
    REQUIRE( blendMultibandWindow( a, b, one, w, h, 3, &out ) );
    for ( size_t i = 0; i < out.size(); ++i )
        CHECK( out[i] == Approx( b[i] ).margin( 1e-3 ) );

    REQUIRE( blendMultibandWindow( a, b, zero, w, h, 3, &out ) );
    for ( size_t i = 0; i < out.size(); ++i )
        CHECK( out[i] == Approx( a[i] ).margin( 1e-3 ) );
}

TEST_CASE( "Multiband: output stays within the convex hull of both inputs (halo guard)",
           "[processing][mosaic][blend]" )
{
    // High-contrast step edge in both inputs, smooth ramp weight across.
    const int w = 32, h = 32;
    std::vector<float> a( static_cast<size_t>( w ) * h );
    std::vector<float> b( static_cast<size_t>( w ) * h );
    std::vector<double> wB( static_cast<size_t>( w ) * h );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
        {
            const size_t i = static_cast<size_t>( r ) * w + c;
            a[i] = ( c < w / 2 ) ? 5.0f : 120.0f;
            b[i] = a[i] + 40.0f;
            wB[i] = static_cast<double>( c ) / ( w - 1 );
        }

    std::vector<float> out;
    REQUIRE( blendMultibandWindow( a, b, wB, w, h, 4, &out ) );
    for ( size_t i = 0; i < out.size(); ++i )
    {
        const double lo = std::min( a[i], b[i] );
        const double hi = std::max( a[i], b[i] );
        CHECK( out[i] >= Approx( lo ).margin( 1e-2 ) );
        CHECK( out[i] <= Approx( hi ).margin( 1e-2 ) );
    }
}

TEST_CASE( "Multiband: invalid arguments rejected", "[processing][mosaic][blend]" )
{
    std::vector<float> out;
    std::vector<float> a( 4, 1.0f ), b( 4, 2.0f );
    std::vector<double> wB( 4, 0.5 );
    CHECK_FALSE( blendMultibandWindow( a, b, wB, -1, 2, 2, &out ) );
    CHECK_FALSE( blendMultibandWindow( a, b, wB, 2, 2, 0, &out ) );
    std::vector<float> other( 3, 1.0f );
    CHECK_FALSE( blendMultibandWindow( a, other, wB, 2, 2, 2, &out ) );
}
