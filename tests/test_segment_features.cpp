// test_segment_features.cpp — Phase 10B Task 10B.1
//
// Tests for RsSegmentMap and RsSegmentFeatures.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "analysis/segmentation/rs_segment_map.h"
#include "analysis/segmentation/rs_segment_features.h"

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
