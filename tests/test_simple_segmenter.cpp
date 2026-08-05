// test_simple_segmenter.cpp — Phase 10B Task 10B.3
//
// Tests for the fallback segmenter (Gaussian + quantize + connected components).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "analysis/segmentation/rs_simple_segmenter.h"

#include <cmath>

using Catch::Approx;

TEST_CASE( "SimpleSegmenter: uniform image produces single segment", "[segmentation][simple]" )
{
    // 8x8 image, all values = 100.0
    const int w = 8, h = 8;
    QVector<float> data( w * h, 100.0f );

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 16;
    params.minRegionSize = 10;

    auto segMap = RsSimpleSegmenter::segment( data.data(), w, h, -9999.0f, params );

    REQUIRE( !segMap.isEmpty() );
    REQUIRE( segMap.width() == w );
    REQUIRE( segMap.height() == h );
    // Uniform image → 1 segment
    REQUIRE( segMap.segmentCount() == 1 );
}

TEST_CASE( "SimpleSegmenter: checkerboard produces multiple segments", "[segmentation][simple]" )
{
    // 8x8 checkerboard: alternating 0 and 255
    const int w = 8, h = 8;
    QVector<float> data( w * h );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
            data[r * w + c] = ( ( r + c ) % 2 == 0 ) ? 0.0f : 255.0f;

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3; // small kernel preserves checkerboard
    params.quantizeBins = 2;
    params.minRegionSize = 1; // don't merge

    auto segMap = RsSimpleSegmenter::segment( data.data(), w, h, -9999.0f, params );

    REQUIRE( !segMap.isEmpty() );
    // With small smoothing, should see more than 1 segment
    REQUIRE( segMap.segmentCount() > 1 );
}

TEST_CASE( "SimpleSegmenter: minRegionSize merges small regions", "[segmentation][simple]" )
{
    // Create image with one large region and one tiny region
    const int w = 10, h = 10;
    QVector<float> data( w * h, 100.0f );
    // Small 2x2 block with different value
    data[0] = 200.0f; data[1] = 200.0f;
    data[10] = 200.0f; data[11] = 200.0f;

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 2;   // coarse quantization preserves uniform regions
    params.minRegionSize = 50; // merge regions < 50 pixels

    auto segMap = RsSimpleSegmenter::segment( data.data(), w, h, -9999.0f, params );

    REQUIRE( !segMap.isEmpty() );
    // With high minRegionSize, small block should be merged → 1 segment
    REQUIRE( segMap.segmentCount() == 1 );
}

TEST_CASE( "SimpleSegmenter: nodata handling", "[segmentation][simple]" )
{
    const int w = 6, h = 6;
    QVector<float> data( w * h, 100.0f );
    // Set some pixels to nodata
    data[0] = -9999.0f;
    data[1] = -9999.0f;

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 8;
    params.minRegionSize = 5;

    auto segMap = RsSimpleSegmenter::segment( data.data(), w, h, -9999.0f, params );

    REQUIRE( !segMap.isEmpty() );
    // Nodata pixels should have label 0
    REQUIRE( segMap.labelAt( 0, 0 ) == 0 );
    REQUIRE( segMap.labelAt( 0, 1 ) == 0 );
    // Non-nodata pixels should have a segment
    REQUIRE( segMap.labelAt( 1, 1 ) != 0 );
}

TEST_CASE( "SimpleSegmenter: multi-band segmentation", "[segmentation][simple]" )
{
    const int w = 6, h = 6, nBands = 3;
    QVector<float> band0( w * h, 50.0f );
    QVector<float> band1( w * h, 100.0f );
    QVector<float> band2( w * h, 150.0f );
    const float *bands[] = { band0.data(), band1.data(), band2.data() };

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 8;
    params.minRegionSize = 5;

    auto segMap = RsSimpleSegmenter::segmentMultiBand( bands, nBands, w, h, -9999.0f, params );

    REQUIRE( !segMap.isEmpty() );
    // Uniform across all bands → 1 segment
    REQUIRE( segMap.segmentCount() == 1 );
}

TEST_CASE( "SimpleSegmenter: empty input returns empty map", "[segmentation][simple]" )
{
    RsSimpleSegmenter::Params params;
    auto segMap = RsSimpleSegmenter::segment( nullptr, 0, 0, -9999.0f, params );
    REQUIRE( segMap.isEmpty() );
}

// ADR 0060 — operator callers drive cancel/progress through RSOperatorContext
// hooks; the segmenter must honor them.

TEST_CASE( "SimpleSegmenter: cancel before work returns empty map", "[segmentation][simple]" )
{
    const int w = 8, h = 8;
    QVector<float> data( w * h, 100.0f );

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 16;
    params.minRegionSize = 10;

    bool cancel = true;
    auto segMap = RsSimpleSegmenter::segment( data.data(), w, h, -9999.0f, params,
                                              [&cancel]() { return cancel; } );
    REQUIRE( segMap.isEmpty() );
}

TEST_CASE( "SimpleSegmenter: cancel mid-pipeline returns empty map", "[segmentation][simple]" )
{
    // 512x512 checkerboard — enough work that the phase/CC cancel polls run.
    const int w = 512, h = 512;
    QVector<float> data( w * h );
    for ( int i = 0; i < data.size(); ++i )
        data[i] = ( i % 2 == 0 ) ? 0.0f : 255.0f;

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 2;
    params.minRegionSize = 1;

    // Turn cancellation on at the second poll (after the start check), i.e.
    // mid-pipeline — the result must still be an empty map.
    int polls = 0;
    auto segMap = RsSimpleSegmenter::segment(
        data.data(), w, h, -9999.0f, params,
        [&polls]() { return ++polls >= 2; } );
    REQUIRE( segMap.isEmpty() );
}

TEST_CASE( "SimpleSegmenter: progress hook covers the full range", "[segmentation][simple]" )
{
    const int w = 8, h = 8;
    QVector<float> data( w * h, 100.0f );

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 16;
    params.minRegionSize = 10;

    float first = -1.0f, last = -1.0f;
    int calls = 0;
    auto segMap = RsSimpleSegmenter::segment(
        data.data(), w, h, -9999.0f, params, {},
        [&]( float f ) {
            if ( calls == 0 )
                first = f;
            last = f;
            ++calls;
        } );
    REQUIRE( !segMap.isEmpty() );
    REQUIRE( calls >= 2 );
    REQUIRE( first == Approx( 0.0f ) );
    REQUIRE( last == Approx( 1.0f ) );
}

TEST_CASE( "SimpleSegmenter: multi-band progress is scaled to the whole call", "[segmentation][simple]" )
{
    const int w = 6, h = 6, nBands = 2;
    QVector<float> band0( w * h, 50.0f );
    QVector<float> band1( w * h, 150.0f );
    const float *bands[] = { band0.data(), band1.data() };

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 8;
    params.minRegionSize = 5;

    float first = -1.0f, last = -1.0f;
    int calls = 0;
    auto segMap = RsSimpleSegmenter::segmentMultiBand(
        bands, nBands, w, h, -9999.0f, params, {},
        [&]( float f ) {
            if ( calls == 0 )
                first = f;
            last = f;
            ++calls;
        } );
    REQUIRE( !segMap.isEmpty() );
    REQUIRE( first == Approx( 0.0f ) );
    REQUIRE( last == Approx( 1.0f ) );
}
