// test_obia_segmentation.cpp — Comprehensive OBIA Baatz-Schäpe Multiresolution Segmentation & Helper Tests
#include <catch2/catch_test_macros.hpp>

#include "analysis/segmentation/rs_multires_segmenter.h"
#include "analysis/segmentation/rs_segment_features.h"
#include "analysis/segmentation/rs_segment_map.h"

#include <gdal.h>

#include <QTemporaryDir>

#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

static bool g_gdalInit = ( GDALAllRegister(), true );

namespace
{

QString createTestRaster( const QString &dir, int w, int h )
{
    QString path = dir + "/seg_input.tif";
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};

    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr );
    if ( !ds )
        return {};

    QVector<float> row( w );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
            row[c] = ( c < w / 2 ) ? 50.0f : 150.0f;
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        GDALRasterIO( band, GF_Write, 0, r, w, 1, row.data(), w, 1, GDT_Float32, 0, 0 );
    }

    GDALClose( ds );
    return path;
}

QString createMultibandRaster( const QString &dir, int w, int h, int bands )
{
    QString path = dir + "/multiband_seg_input.tif";
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};

    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, bands, GDT_Float32, nullptr );
    if ( !ds )
        return {};

    std::vector<float> row( w );
    for ( int b = 1; b <= bands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b );
        for ( int r = 0; r < h; ++r )
        {
            for ( int c = 0; c < w; ++c )
            {
                if ( b == 1 )
                    row[c] = ( c < w / 2 ) ? 20.0f : 80.0f;       // Vertical split
                else if ( b == 2 )
                    row[c] = ( r < h / 2 ) ? 30.0f : 90.0f;       // Horizontal split
                else
                    row[c] = 50.0f;                               // Uniform
            }
            GDALRasterIO( band, GF_Write, 0, r, w, 1, row.data(), w, 1, GDT_Float32, 0, 0 );
        }
    }

    GDALClose( ds );
    return path;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Native Baatz-Schäpe Multiresolution Segmentation Test Suite (M2)
// ---------------------------------------------------------------------------

TEST_CASE( "RsMultiresSegmenter: Exact scale parameter bounding", "[obia][segmentation][mrs][scale]" )
{
    // 16x16 image with 2 distinct homogeneous blocks:
    // Left half (columns 0..7) = 10.0f, Right half (columns 8..15) = 60.0f.
    const int w = 16;
    const int h = 16;
    std::vector<float> buffer( w * h );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            buffer[r * w + c] = ( c < w / 2 ) ? 10.0f : 60.0f;
        }
    }
    const float *bandData[1] = { buffer.data() };

    // With very small scale (S = 1.0, S^2 = 1.0): the two halves cannot merge.
    // Within each homogeneous half, variance is 0, so all pixels in each half merge.
    // Expected segments = 2 (left block and right block).
    RsMultiresParams fineParams;
    fineParams.scale = 1.0;
    fineParams.shapeWeight = 0.0; // pure spectral color
    fineParams.minRegionSize = 1;

    RsSegmentMap fineMap = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, fineParams );
    REQUIRE( !fineMap.isEmpty() );
    REQUIRE( fineMap.segmentCount() == 2 );

    // Verify segment assignments: all left pixels share one ID, all right pixels share another ID
    quint32 leftLabel = fineMap.labelAt( 0, 0 );
    quint32 rightLabel = fineMap.labelAt( 0, w - 1 );
    REQUIRE( leftLabel != 0 );
    REQUIRE( rightLabel != 0 );
    REQUIRE( leftLabel != rightLabel );

    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            if ( c < w / 2 )
                CHECK( fineMap.labelAt( r, c ) == leftLabel );
            else
                CHECK( fineMap.labelAt( r, c ) == rightLabel );
        }
    }

    // With coarse scale (S = 100.0, S^2 = 10000.0): the fusion cost deltaH <= S^2 allows merging across the boundary.
    RsMultiresParams coarseParams;
    coarseParams.scale = 100.0;
    coarseParams.shapeWeight = 0.0;
    coarseParams.minRegionSize = 1;

    RsSegmentMap coarseMap = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, coarseParams );
    REQUIRE( !coarseMap.isEmpty() );
    REQUIRE( coarseMap.segmentCount() == 1 );
    CHECK( fineMap.segmentCount() > coarseMap.segmentCount() );
}

TEST_CASE( "RsMultiresSegmenter: Monotonic coarsening on gradient raster", "[obia][segmentation][mrs][monotonic]" )
{
    // Continuous diagonal gradient: val = r * 4.0 + c * 4.0
    const int w = 24;
    const int h = 24;
    std::vector<float> buffer( w * h );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            buffer[r * w + c] = static_cast<float>( r * 4 + c * 4 );
        }
    }
    const float *bandData[1] = { buffer.data() };

    RsMultiresParams p1;
    p1.scale = 5.0;
    p1.shapeWeight = 0.1;
    RsSegmentMap map1 = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, p1 );

    RsMultiresParams p2;
    p2.scale = 20.0;
    p2.shapeWeight = 0.1;
    RsSegmentMap map2 = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, p2 );

    RsMultiresParams p3;
    p3.scale = 60.0;
    p3.shapeWeight = 0.1;
    RsSegmentMap map3 = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, p3 );

    REQUIRE( !map1.isEmpty() );
    REQUIRE( !map2.isEmpty() );
    REQUIRE( !map3.isEmpty() );

    // As scale increases, segment count monotonically decreases
    CHECK( map1.segmentCount() >= map2.segmentCount() );
    CHECK( map2.segmentCount() >= map3.segmentCount() );
}

TEST_CASE( "RsMultiresSegmenter: Color weight vs shape weight effect", "[obia][segmentation][mrs][weights]" )
{
    // Synthetic 32x32 image with high spectral noise in foreground but compact square boundary
    const int w = 32;
    const int h = 32;
    std::vector<float> buffer( w * h );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            bool inSquare = ( r >= 8 && r < 24 && c >= 8 && c < 24 );
            if ( inSquare )
            {
                // Alternating high/low spectral pattern inside the square
                buffer[r * w + c] = ( ( r + c ) % 2 == 0 ) ? 80.0f : 120.0f;
            }
            else
            {
                buffer[r * w + c] = 10.0f;
            }
        }
    }
    const float *bandData[1] = { buffer.data() };

    // Pure color weight (w_shape = 0.0, w_color = 1.0): spectral difference inside square prevents merging
    RsMultiresParams colorParams;
    // scale 5 (threshold 25): the 80/120 checkerboard pair-merge spectral cost
    // (delta ~40) exceeds the threshold, so pure-color weighting cannot merge
    // inside the square and stays fine-grained.
    colorParams.scale = 5.0;
    colorParams.shapeWeight = 0.0;
    colorParams.compactnessWeight = 0.5;
    colorParams.minRegionSize = 1;

    RsSegmentMap colorMap = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, colorParams );
    REQUIRE( !colorMap.isEmpty() );

    // High shape weight (w_shape = 0.8, w_color = 0.2): shape homogeneity drives merging of compact square
    RsMultiresParams shapeParams;
    // Same scale, but the 0.2 color share (~8) plus the small shape term
    // (~0.4) stays below 25, so shape weighting merges the compact square.
    shapeParams.scale = 5.0;
    shapeParams.shapeWeight = 0.8;
    shapeParams.compactnessWeight = 0.5;
    shapeParams.minRegionSize = 1;

    RsSegmentMap shapeMap = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, shapeParams );
    REQUIRE( !shapeMap.isEmpty() );

    // High shape weight should produce coarser/fewer segments by regularizing geometry
    CHECK( colorMap.segmentCount() >= shapeMap.segmentCount() );
}

TEST_CASE( "RsMultiresSegmenter: Compactness vs smoothness effect", "[obia][segmentation][mrs][compactness]" )
{
    const int w = 32;
    const int h = 32;
    std::vector<float> buffer( w * h );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            // Elongated horizontal bar with slight noise
            bool inBar = ( r >= 12 && r < 20 );
            buffer[r * w + c] = inBar ? ( 100.0f + static_cast<float>( c % 3 ) * 5.0f ) : 20.0f;
        }
    }
    const float *bandData[1] = { buffer.data() };

    // Compactness heavy (w_comp = 0.95): penalizes elongated boundary relative to sqrt(area)
    RsMultiresParams compParams;
    compParams.scale = 18.0;
    compParams.shapeWeight = 0.6;
    compParams.compactnessWeight = 0.95;
    compParams.minRegionSize = 1;

    RsSegmentMap compMap = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, compParams );
    REQUIRE( !compMap.isEmpty() );

    // Smoothness heavy (w_comp = 0.05): allows elongated bounding-box aligned shapes
    RsMultiresParams smoothParams;
    smoothParams.scale = 18.0;
    smoothParams.shapeWeight = 0.6;
    smoothParams.compactnessWeight = 0.05;
    smoothParams.minRegionSize = 1;

    RsSegmentMap smoothMap = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, smoothParams );
    REQUIRE( !smoothMap.isEmpty() );

    // Both should produce valid segmented representations
    CHECK( compMap.segmentCount() > 0 );
    CHECK( smoothMap.segmentCount() > 0 );
}

TEST_CASE( "RsMultiresSegmenter: Multi-band vs single-band segmentation", "[obia][segmentation][mrs][multiband]" )
{
    const int w = 20;
    const int h = 20;
    std::vector<float> band1( w * h ); // Left (10) vs Right (100)
    std::vector<float> band2( w * h ); // Top (10) vs Bottom (100)
    std::vector<float> band3( w * h, 50.0f ); // Uniform

    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            band1[r * w + c] = ( c < w / 2 ) ? 10.0f : 100.0f;
            band2[r * w + c] = ( r < h / 2 ) ? 10.0f : 100.0f;
        }
    }

    // 1. Single band 1: exactly 2 vertical halves
    const float *b1Ptr[1] = { band1.data() };
    RsMultiresParams pSingle;
    pSingle.scale = 5.0;
    pSingle.shapeWeight = 0.0;
    RsSegmentMap mapB1 = RsMultiresSegmenter::segment( b1Ptr, 1, w, h, -9999.0f, pSingle );
    REQUIRE( mapB1.segmentCount() == 2 );

    // 2. Single band 2: exactly 2 horizontal halves
    const float *b2Ptr[1] = { band2.data() };
    RsSegmentMap mapB2 = RsMultiresSegmenter::segment( b2Ptr, 1, w, h, -9999.0f, pSingle );
    REQUIRE( mapB2.segmentCount() == 2 );

    // 3. Multi-band 1 & 2: detects both vertical and horizontal boundaries -> 4 quadrants!
    const float *mbPtr[2] = { band1.data(), band2.data() };
    RsMultiresParams pMulti;
    pMulti.scale = 5.0;
    pMulti.shapeWeight = 0.0;
    RsSegmentMap mapMulti = RsMultiresSegmenter::segment( mbPtr, 2, w, h, -9999.0f, pMulti );
    REQUIRE( mapMulti.segmentCount() == 4 );

    // Verify 4 quadrant labels
    quint32 qTopLeft = mapMulti.labelAt( 0, 0 );
    quint32 qTopRight = mapMulti.labelAt( 0, w - 1 );
    quint32 qBottomLeft = mapMulti.labelAt( h - 1, 0 );
    quint32 qBottomRight = mapMulti.labelAt( h - 1, w - 1 );

    CHECK( qTopLeft != qTopRight );
    CHECK( qTopLeft != qBottomLeft );
    CHECK( qTopLeft != qBottomRight );
    CHECK( qTopRight != qBottomLeft );
    CHECK( qTopRight != qBottomRight );
    CHECK( qBottomLeft != qBottomRight );

    // 4. Custom band weights: weight band1 = 1.0, band2 = 0.0 -> yields 2 vertical segments
    RsMultiresParams pWeighted;
    pWeighted.scale = 5.0;
    pWeighted.shapeWeight = 0.0;
    pWeighted.bandWeights = { 1.0, 0.0 };
    RsSegmentMap mapWeighted = RsMultiresSegmenter::segment( mbPtr, 2, w, h, -9999.0f, pWeighted );
    REQUIRE( mapWeighted.segmentCount() == 2 );
}

TEST_CASE( "RsMultiresSegmenter: Minimum region size cleanup (MMU)", "[obia][segmentation][mrs][mmu]" )
{
    // 20x20 raster with single isolated pixel anomalies
    const int w = 20;
    const int h = 20;
    std::vector<float> buffer( w * h, 10.0f );
    // Single isolated pixel anomalies
    buffer[5 * w + 5] = 200.0f;
    buffer[15 * w + 15] = 250.0f;

    const float *bandData[1] = { buffer.data() };

    // With minRegionSize = 1, isolated anomaly pixels form separate segments
    RsMultiresParams pNoMmu;
    pNoMmu.scale = 5.0;
    pNoMmu.shapeWeight = 0.0;
    pNoMmu.minRegionSize = 1;
    RsSegmentMap mapNoMmu = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, pNoMmu );
    REQUIRE( mapNoMmu.segmentCount() == 3 ); // background + 2 anomalies

    // With minRegionSize = 5, anomalies must be merged into background
    RsMultiresParams pMmu;
    pMmu.scale = 5.0;
    pMmu.shapeWeight = 0.0;
    pMmu.minRegionSize = 5;
    RsSegmentMap mapMmu = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, pMmu );
    REQUIRE( mapMmu.segmentCount() == 1 ); // merged to 1 segment
    CHECK( mapMmu.pixelCount( 1 ) == w * h );
}

TEST_CASE( "RsMultiresSegmenter: NoData handling & topological invariants", "[obia][segmentation][mrs][nodata]" )
{
    const int w = 16;
    const int h = 16;
    std::vector<float> buffer( w * h, 50.0f );
    const float nodataVal = -9999.0f;

    // Set border as NoData
    int nodataCount = 0;
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            if ( r == 0 || r == h - 1 || c == 0 || c == w - 1 )
            {
                buffer[r * w + c] = nodataVal;
                nodataCount++;
            }
        }
    }
    const float *bandData[1] = { buffer.data() };

    RsMultiresParams params;
    params.scale = 10.0;
    RsSegmentMap segMap = RsMultiresSegmenter::segment( bandData, 1, w, h, nodataVal, params );
    REQUIRE( !segMap.isEmpty() );

    // Verify all NoData pixels have label 0
    int zeroLabels = 0;
    int validLabels = 0;
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            quint32 lbl = segMap.labelAt( r, c );
            if ( r == 0 || r == h - 1 || c == 0 || c == w - 1 )
            {
                CHECK( lbl == 0 );
                zeroLabels++;
            }
            else
            {
                CHECK( lbl > 0 );
                validLabels++;
            }
        }
    }
    CHECK( zeroLabels == nodataCount );
    CHECK( validLabels == ( w * h - nodataCount ) );
}

TEST_CASE( "RsMultiresSegmenter: Cancellation and progress hooks", "[obia][segmentation][mrs][cancel]" )
{
    const int w = 16;
    const int h = 16;
    std::vector<float> buffer( w * h, 42.0f );
    const float *bandData[1] = { buffer.data() };

    // 1. Pre-canceled callback returns empty map
    auto isCanceled = []() { return true; };
    RsMultiresParams params;
    RsSegmentMap canceledMap = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, params, isCanceled );
    REQUIRE( canceledMap.isEmpty() );

    // 2. Progress reporting
    float maxProgress = 0.0f;
    auto onProgress = [&]( float p ) {
        if ( p > maxProgress ) maxProgress = p;
    };
    RsSegmentMap normalMap = RsMultiresSegmenter::segment( bandData, 1, w, h, -9999.0f, params, {}, onProgress );
    REQUIRE( !normalMap.isEmpty() );
    CHECK( maxProgress >= 1.0f );
}

TEST_CASE( "RsMultiresSegmenter: GDAL raster file execution & RsSegmenterPort", "[obia][segmentation][mrs][gdal]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const QString rasterPath = createMultibandRaster( tempDir.path(), 24, 24, 3 );
    REQUIRE( !rasterPath.isEmpty() );

    // 1. Direct segmentRasterFile
    RsMultiresParams params;
    params.scale = 10.0;
    params.shapeWeight = 0.2;
    params.compactnessWeight = 0.5;

    QString errorMsg;
    RsSegmentMap segMap = RsMultiresSegmenter::segmentRasterFile( rasterPath, { 1, 2, 3 }, params, {}, {}, &errorMsg );
    REQUIRE( errorMsg.isEmpty() );
    REQUIRE( !segMap.isEmpty() );
    REQUIRE( segMap.segmentCount() >= 1 );

    // 2. RsSegmenterPort polymorphic interface
    RsMultiresSegmenter segmenter;
    RsLevelSpec spec;
    spec.rangeRadius = 15.0;
    spec.minRegionSize = 4;

    RsSegmenterResult portResult = segmenter.segment( rasterPath, spec );
    REQUIRE( portResult.ok );
    REQUIRE( !portResult.segMap.isEmpty() );
    REQUIRE( portResult.segMap.segmentCount() >= 1 );
}

TEST_CASE( "RsMultiresSegmenter: Performance & scalability (256x256 multi-band raster)", "[obia][segmentation][mrs][perf]" )
{
    const int w = 256;
    const int h = 256;
    const size_t totalPixels = static_cast<size_t>( w * h );
    std::vector<float> b1( totalPixels );
    std::vector<float> b2( totalPixels );

    // Create a rich synthetic scene with multiple geometric regions and textures
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
        {
            size_t idx = static_cast<size_t>( r * w + c );
            int blockR = r / 32;
            int blockC = c / 32;
            b1[idx] = static_cast<float>( ( blockR * 7 + blockC * 13 ) % 256 ) + static_cast<float>( ( r + c ) % 5 );
            b2[idx] = static_cast<float>( ( blockR * 11 + blockC * 3 ) % 256 ) + static_cast<float>( ( r * c ) % 7 );
        }
    }

    const float *bands[2] = { b1.data(), b2.data() };

    RsMultiresParams params;
    params.scale = 25.0;
    params.shapeWeight = 0.2;
    params.compactnessWeight = 0.5;
    params.minRegionSize = 10;

    auto t0 = std::chrono::high_resolution_clock::now();
    RsSegmentMap map = RsMultiresSegmenter::segment( bands, 2, w, h, -9999.0f, params );
    auto t1 = std::chrono::high_resolution_clock::now();

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();

    REQUIRE( !map.isEmpty() );
    REQUIRE( map.segmentCount() > 0 );
    REQUIRE( map.width() == w );
    REQUIRE( map.height() == h );

    // Topological invariant: sum of all segment pixel counts strictly equals total valid pixels
    int totalCounted = 0;
    for ( quint32 label : map.uniqueLabels() )
    {
        totalCounted += map.pixelCount( label );
    }
    CHECK( totalCounted == w * h );

    // Performance assertion: 256x256 (65,536 pixels) 2-band MRS segmentation completes well within 5 seconds
    CHECK( elapsedMs < 5000 );
}

TEST_CASE( "OBIA segment features 64-bit label indexing safety (#472)", "[obia][segmentation][features][472]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const QString inputPath = createTestRaster( tempDir.path(), 8, 8 );
    REQUIRE( !inputPath.isEmpty() );

    QVector<quint32> labels( 64, 1 );
    for ( int i = 32; i < 64; ++i )
        labels[i] = 2;
    RsSegmentMap segMap( labels, 8, 8 );
    REQUIRE( segMap.segmentCount() == 2 );

    auto features = RsSegmentFeatures::extract( inputPath, segMap, { 1 } );
    REQUIRE( features.contains( 1 ) );
    REQUIRE( features.contains( 2 ) );
    CHECK( features[1].area == 32.0 );
    CHECK( features[2].area == 32.0 );
}
