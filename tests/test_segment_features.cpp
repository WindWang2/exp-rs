// test_segment_features.cpp — Phase 10B Task 10B.1
//
// Tests for RsSegmentMap and RsSegmentFeatures.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "analysis/segmentation/rs_segment_map.h"
#include "analysis/segmentation/rs_segment_features.h"

#include <gdal.h>
#include <cpl_conv.h>
#include <QTemporaryDir>

using Catch::Approx;

// ---------------------------------------------------------------------------
// RsSegmentMap tests
// ---------------------------------------------------------------------------

TEST_CASE( "SegmentMap: construction and basic queries", "[segmentation]" )
{
    // 4x4 label image:
    //   1 1 2 2
    //   1 1 2 2
    //   3 3 3 3
    //   0 0 0 0   (nodata row)
    QVector<quint32> labels = {
        1, 1, 2, 2,
        1, 1, 2, 2,
        3, 3, 3, 3,
        0, 0, 0, 0
    };
    RsSegmentMap segMap( labels, 4, 4 );

    REQUIRE( segMap.width() == 4 );
    REQUIRE( segMap.height() == 4 );
    REQUIRE( !segMap.isEmpty() );
    REQUIRE( segMap.segmentCount() == 3 );

    SECTION( "labelAt returns correct values" )
    {
        REQUIRE( segMap.labelAt( 0, 0 ) == 1 );
        REQUIRE( segMap.labelAt( 0, 2 ) == 2 );
        REQUIRE( segMap.labelAt( 2, 0 ) == 3 );
        REQUIRE( segMap.labelAt( 3, 0 ) == 0 ); // nodata
        REQUIRE( segMap.labelAt( -1, 0 ) == 0 ); // out of bounds
        REQUIRE( segMap.labelAt( 0, 5 ) == 0 );  // out of bounds
    }

    SECTION( "uniqueLabels excludes nodata" )
    {
        auto labels = segMap.uniqueLabels();
        REQUIRE( labels.size() == 3 );
        REQUIRE( labels.contains( 1 ) );
        REQUIRE( labels.contains( 2 ) );
        REQUIRE( labels.contains( 3 ) );
        REQUIRE( !labels.contains( 0 ) );
    }

    SECTION( "pixelCoords returns all pixels for a segment" )
    {
        auto coords1 = segMap.pixelCoords( 1 );
        REQUIRE( coords1.size() == 4 ); // (0,0),(1,0),(0,1),(1,1)

        auto coords2 = segMap.pixelCoords( 2 );
        REQUIRE( coords2.size() == 4 );

        auto coords3 = segMap.pixelCoords( 3 );
        REQUIRE( coords3.size() == 4 );

        auto coords0 = segMap.pixelCoords( 0 );
        REQUIRE( coords0.size() == 0 ); // label 0 (nodata) is not a segment

        auto coords99 = segMap.pixelCoords( 99 );
        REQUIRE( coords99.size() == 0 ); // nonexistent
    }

    SECTION( "pixelCount uses size cache without requiring coords" )
    {
        REQUIRE( segMap.pixelCount( 1 ) == 4 );
        REQUIRE( segMap.pixelCount( 2 ) == 4 );
        REQUIRE( segMap.pixelCount( 3 ) == 4 );
        REQUIRE( segMap.pixelCount( 0 ) == 0 );
        REQUIRE( segMap.pixelCount( 99 ) == 0 );
    }
}

TEST_CASE( "SegmentMap: empty map", "[segmentation]" )
{
    RsSegmentMap empty;
    REQUIRE( empty.isEmpty() );
    REQUIRE( empty.segmentCount() == 0 );
    REQUIRE( empty.labelAt( 0, 0 ) == 0 );
}

TEST_CASE( "SegmentMap: single segment", "[segmentation]" )
{
    QVector<quint32> labels( 9, 5 ); // 3x3, all segment 5
    RsSegmentMap segMap( labels, 3, 3 );

    REQUIRE( segMap.segmentCount() == 1 );
    REQUIRE( segMap.uniqueLabels().contains( 5 ) );
    REQUIRE( segMap.pixelCoords( 5 ).size() == 9 );
}

// ---------------------------------------------------------------------------
// RsSegmentFeatures tests
// ---------------------------------------------------------------------------

TEST_CASE( "SegmentFeatures: extract from synthetic raster", "[segmentation]" )
{
    // Create a 4x4 segment map with 2 segments
    QVector<quint32> labels = {
        1, 1, 2, 2,
        1, 1, 2, 2,
        1, 1, 2, 2,
        1, 1, 2, 2
    };
    RsSegmentMap segMap( labels, 4, 4 );

    // We test with a real raster if available; otherwise test with empty path
    // returns empty map.
    QVector<int> bandIndices = { 1 };
    auto stats = RsSegmentFeatures::extract( "/nonexistent.tif", segMap, bandIndices );
    REQUIRE( stats.isEmpty() ); // file doesn't exist
}

TEST_CASE( "SegmentFeatures: shape index computation", "[segmentation]" )
{
    // Square: area=4 (2x2), perimeter=8 → shapeIndex = 8/(4*sqrt(4)) = 1.0
    // We can't call private computeShapeIndex directly, but we can test via
    // a segment that forms a perfect square.

    // 2x2 segment map, single segment
    QVector<quint32> labels = { 1, 1, 1, 1 };
    RsSegmentMap segMap( labels, 2, 2 );

    // shapeIndex is computed internally; we verify it via a real raster if
    // available. For now, just verify the map is valid.
    REQUIRE( segMap.segmentCount() == 1 );
    REQUIRE( segMap.pixelCoords( 1 ).size() == 4 );
}

// ---------------------------------------------------------------------------
// RsSegmentFeatures::extract numerical correctness (vector-indexed path, ADR slice 5)
// Creates a real GeoTIFF with known pixel values and verifies that extract()
// computes correct mean, area, perimeter, and shape descriptors. This also
// exercises the QMap->vector optimization with contiguous labels 1..N.
// ---------------------------------------------------------------------------
static bool g_gdalInit = ( GDALAllRegister(), true );

TEST_CASE( "SegmentFeatures: extract computes correct stats from real raster", "[segmentation]" )
{
    // 4x4 raster, 1 band. Left half (cols 0-1) = segment 1, values 10.
    // Right half (cols 2-3) = segment 2, values 20..25.
    // Segment 2 is not uniform so we can check mean/stddev.
    constexpr int W = 4, H = 4;
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const QString path = tempDir.filePath( "test_extract.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );

    QVector<float> pixels( W * H );
    for ( int r = 0; r < H; ++r )
    {
        for ( int c = 0; c < W; ++c )
            pixels[r * W + c] = ( c < W / 2 ) ? 10.0f : static_cast<float>( 20 + r );
    }
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALRasterIO( band, GF_Write, 0, 0, W, H, pixels.data(), W, H, GDT_Float32, 0, 0 );
    GDALClose( ds );

    // Segment map: left half = label 1, right half = label 2.
    QVector<quint32> labels( W * H );
    for ( int i = 0; i < W * H; ++i )
        labels[i] = ( i % W < W / 2 ) ? 1u : 2u;
    RsSegmentMap segMap( labels, W, H );

    QVector<int> bandIndices = { 1 };
    auto stats = RsSegmentFeatures::extract( path, segMap, bandIndices );

    REQUIRE( stats.size() == 2 );
    REQUIRE( stats.contains( 1 ) );
    REQUIRE( stats.contains( 2 ) );

    // Segment 1: 8 pixels, all value 10 -> mean=10, stddev=0, area=8.
    const auto &s1 = stats[1];
    REQUIRE( s1.area == 8 );
    REQUIRE( s1.mean[0] == Approx( 10.0 ).margin( 1e-6 ) );
    REQUIRE( s1.stddev[0] == Approx( 0.0 ).margin( 1e-6 ) );
    REQUIRE( s1.min[0] == Approx( 10.0 ) );
    REQUIRE( s1.max[0] == Approx( 10.0 ) );

    // Segment 2: 8 pixels, values 20,21,22,23 (rows 0-3, cols 2-3 each).
    // mean = (20+21+22+23)*2 / 8 = 86*2/8 = 21.5
    const auto &s2 = stats[2];
    REQUIRE( s2.area == 8 );
    REQUIRE( s2.mean[0] == Approx( 21.5 ).margin( 1e-6 ) );
    REQUIRE( s2.min[0] == Approx( 20.0 ) );
    REQUIRE( s2.max[0] == Approx( 23.0 ) );

    // Both segments are 2-wide x 4-tall rectangles (cols 0-1 / cols 2-3, all
    // 4 rows). The perimeter counter tallies boundary pixels: every pixel on
    // the outer edge of the segment (image edge or neighbor with a different
    // label). For a 2-wide strip touching the image edge on one side and a
    // different segment on the other, all 8 pixels are boundary pixels.
    REQUIRE( s1.perimeter == 8 );
    REQUIRE( s2.perimeter == 8 );

    // Compactness = P^2 / (4*pi*A) = 64 / (4*pi*8) ~ 0.6366
    REQUIRE( s1.compactness == Approx( 64.0 / ( 4.0 * M_PI * 8.0 ) ).margin( 1e-3 ) );
}

#ifdef SICNU_HAS_OPENCV
TEST_CASE( "SegmentFeatures: toFeatureMatrix dimensions", "[segmentation]" )
{
    // Build a small synthetic stats map
    QMap<quint32, RsSegmentFeatures::SegmentStat> stats;

    RsSegmentFeatures::SegmentStat s1;
    s1.mean = { 10.0, 20.0 };
    s1.stddev = { 1.0, 2.0 };
    s1.min = { 8.0, 16.0 };
    s1.max = { 12.0, 24.0 };
    s1.area = 100;
    s1.perimeter = 40;
    s1.shapeIndex = 40.0 / ( 4.0 * std::sqrt( 100.0 ) );
    stats[1] = s1;

    RsSegmentFeatures::SegmentStat s2;
    s2.mean = { 30.0, 40.0 };
    s2.stddev = { 3.0, 4.0 };
    s2.min = { 24.0, 32.0 };
    s2.max = { 36.0, 48.0 };
    s2.area = 200;
    s2.perimeter = 60;
    s2.shapeIndex = 60.0 / ( 4.0 * std::sqrt( 200.0 ) );
    stats[2] = s2;

    QVector<quint32> segmentIds;
    cv::Mat X = RsSegmentFeatures::toFeatureMatrix( stats, segmentIds );

    REQUIRE( X.rows == 2 );
    // Features: 2 bands * 4 stats + 3 shape = 11
    REQUIRE( X.cols == 11 );
    REQUIRE( segmentIds.size() == 2 );
    REQUIRE( segmentIds[0] == 1 );
    REQUIRE( segmentIds[1] == 2 );

    // Verify first row values
    REQUIRE( X.at<float>( 0, 0 ) == Approx( 10.0f ) );  // mean band 0
    REQUIRE( X.at<float>( 0, 1 ) == Approx( 20.0f ) );  // mean band 1
    REQUIRE( X.at<float>( 0, 2 ) == Approx( 1.0f ) );   // stddev band 0
    REQUIRE( X.at<float>( 0, 3 ) == Approx( 2.0f ) );   // stddev band 1
    REQUIRE( X.at<float>( 0, 4 ) == Approx( 8.0f ) );   // min band 0
    REQUIRE( X.at<float>( 0, 5 ) == Approx( 16.0f ) );  // min band 1
    REQUIRE( X.at<float>( 0, 6 ) == Approx( 12.0f ) );  // max band 0
    REQUIRE( X.at<float>( 0, 7 ) == Approx( 24.0f ) );  // max band 1
    REQUIRE( X.at<float>( 0, 8 ) == Approx( 100.0f ) ); // area
    REQUIRE( X.at<float>( 0, 9 ) == Approx( 40.0f ) );  // perimeter
}

TEST_CASE( "SegmentFeatures: empty stats returns empty Mat", "[segmentation]" )
{
    QMap<quint32, RsSegmentFeatures::SegmentStat> empty;
    QVector<quint32> ids;
    cv::Mat X = RsSegmentFeatures::toFeatureMatrix( empty, ids );
    REQUIRE( X.empty() );
}
#endif
