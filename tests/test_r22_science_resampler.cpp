// test_r22_science_resampler.cpp — R22-01 science track regression anchors
// for #1686: the resampler treated pixel centers as the footprint boundary
// (u > width - 1 → NoData), so the outer half-pixel ring of the source
// footprint — which IS valid coverage — was emitted as NoData. GDAL-style
// footprint semantics are [-0.5, width - 0.5] with edge-clamped taps.
//
// RED provenance: the upsample/edge cases below failed on master@911e0b17
// (see .planning/r22-science/RED_GREEN.md).

#include "processing/algorithms/resampler.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rs::algorithms;

namespace
{

constexpr double kNoData = -9999.0;

/// 4×4 grid f(x, y) = 10x + y (values 0..43, distinct per column).
std::vector<float> rampGrid4()
{
    std::vector<float> grid( 16, 0.0f );
    for ( int y = 0; y < 4; ++y )
        for ( int x = 0; x < 4; ++x )
            grid[static_cast<size_t>( y ) * 4 + x] = static_cast<float>( 10 * x + y );
    return grid;
}

} // namespace

TEST_CASE( "r22/#1686: half-pixel inside footprint is sampled, not NoData",
           "[r22_science][resampler]" )
{
    const auto grid = rampGrid4();

    // u = -0.25 is inside the [-0.5, 3.5] footprint: bilinear edge-clamps to
    // column 0. Old code returned NoData here (issue repro).
    const double bilinearEdge = Resampler::interpolate( grid.data(), 4, 4, -0.25, 1.5,
                                                        ResampleMethod::Bilinear, kNoData );
    REQUIRE( bilinearEdge != kNoData );
    // u=-0.25: weights 0.25 on x=-1 (clamped→0) and 0.75 on x=0 → column 0;
    // v=1.5: mean of rows 1 and 2 → (10*0+1 + 10*0+2)/2 = 1.5.
    REQUIRE_THAT( bilinearEdge, WithinAbs( 1.5, 1e-6 ) );

    // u = 3.25 (right edge, inside footprint) → column 3.
    const double rightEdge = Resampler::interpolate( grid.data(), 4, 4, 3.25, 0.0,
                                                     ResampleMethod::Bilinear, kNoData );
    REQUIRE( rightEdge != kNoData );
    REQUIRE_THAT( rightEdge, WithinAbs( 30.0, 1e-6 ) );

    // Every method samples the footprint boundary ring.
    for ( const auto method : { ResampleMethod::NearestNeighbor, ResampleMethod::Bilinear,
                                ResampleMethod::CubicConvolution, ResampleMethod::Lanczos } ) {
        REQUIRE( Resampler::interpolate( grid.data(), 4, 4, -0.5, 3.5, method, kNoData ) != kNoData );
        REQUIRE( Resampler::interpolate( grid.data(), 4, 4, 3.5, -0.5, method, kNoData ) != kNoData );
    }
}

TEST_CASE( "r22/#1686: beyond the half-pixel footprint is still NoData",
           "[r22_science][resampler]" )
{
    const auto grid = rampGrid4();
    for ( const auto method : { ResampleMethod::NearestNeighbor, ResampleMethod::Bilinear,
                                ResampleMethod::CubicConvolution, ResampleMethod::Lanczos } ) {
        REQUIRE( Resampler::interpolate( grid.data(), 4, 4, -0.5 - 1e-9, 1.5, method, kNoData )
                 == kNoData );
        REQUIRE( Resampler::interpolate( grid.data(), 4, 4, 3.5 + 1e-9, 1.5, method, kNoData )
                 == kNoData );
        REQUIRE( Resampler::interpolate( grid.data(), 4, 4, 1.5, -0.75, method, kNoData )
                 == kNoData );
        REQUIRE( Resampler::interpolate( grid.data(), 4, 4, 1.5, 3.75, method, kNoData )
                 == kNoData );
    }
}

TEST_CASE( "r22/#1686: nearest rounds at half-pixel offsets inside footprint",
           "[r22_science][resampler]" )
{
    const auto grid = rampGrid4();
    // floor(-0.4 + 0.5) = 0 → column 0; floor(3.4 + 0.5) = 3 → column 3
    // (both inside the [-0.5, 3.5] footprint; 3.6 would be outside).
    const double left = Resampler::interpolate( grid.data(), 4, 4, -0.4, 2.0,
                                                ResampleMethod::NearestNeighbor, kNoData );
    REQUIRE_THAT( left, WithinAbs( 2.0, 1e-12 ) );
    const double right = Resampler::interpolate( grid.data(), 4, 4, 3.4, 2.0,
                                                 ResampleMethod::NearestNeighbor, kNoData );
    REQUIRE_THAT( right, WithinAbs( 32.0, 1e-12 ) );
}

TEST_CASE( "r22/#1686: 4x4 -> 8x8 upsample keeps all 64 output cells valid",
           "[r22_science][resampler]" )
{
    const auto src = rampGrid4();
    std::vector<float> dst( 64, static_cast<float>( kNoData ) );
    // Same world footprint, 2× finer pixels.
    const double srcGt[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    const double dstGt[6] = { 0.0, 0.5, 0.0, 0.0, 0.0, -0.5 };

    WarpOptions options;
    options.method = ResampleMethod::Bilinear;
    options.noDataValue = kNoData;
    options.clampRange = false;

    REQUIRE( Resampler::warpRaster( src.data(), 4, 4, srcGt, dst.data(), 8, 8, dstGt,
                                    []( double x, double y ) {
                                        return std::make_pair( x, y );
                                    },
                                    options ) );
    // Old code: the first/last row and column of the output were NoData
    // (u = -0.25 / 3.25 past the pixel-center boundary). New contract: all
    // 64 cells sample inside the footprint.
    int valid = 0;
    for ( const float v : dst )
        if ( v != static_cast<float>( kNoData ) )
            ++valid;
    REQUIRE( valid == 64 );

    // Corners replicate the source corners exactly (edge clamp).
    REQUIRE_THAT( static_cast<double>( dst[0] ), WithinAbs( 0.0, 1e-6 ) );
    REQUIRE_THAT( static_cast<double>( dst[7] ), WithinAbs( 30.0, 1e-6 ) );
    REQUIRE_THAT( static_cast<double>( dst[8 * 7 + 0] ), WithinAbs( 3.0, 1e-6 ) );
    REQUIRE_THAT( static_cast<double>( dst[8 * 7 + 7] ), WithinAbs( 33.0, 1e-6 ) );
}

TEST_CASE( "r22/#1686: sub-pixel translation warp keeps the trailing edge",
           "[r22_science][resampler]" )
{
    // Source shifted by a quarter pixel: without the footprint fix the last
    // output column/row drops to NoData even though it still overlaps the
    // source footprint by 75% of a pixel.
    const auto src = rampGrid4();
    std::vector<float> dst( 16, static_cast<float>( kNoData ) );
    const double srcGt[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    const double dstGt[6] = { 0.25, 1.0, 0.0, 0.25, 0.0, -1.0 };

    WarpOptions options;
    options.method = ResampleMethod::Bilinear;
    options.noDataValue = kNoData;
    options.clampRange = false;

    REQUIRE( Resampler::warpRaster( src.data(), 4, 4, srcGt, dst.data(), 4, 4, dstGt,
                                    []( double x, double y ) {
                                        return std::make_pair( x, y );
                                    },
                                    options ) );
    for ( const float v : dst )
        REQUIRE( v != static_cast<float>( kNoData ) );
}
