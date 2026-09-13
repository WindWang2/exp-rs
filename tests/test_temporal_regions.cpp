// tests/test_temporal_regions.cpp — known-answer tests for the multi-region
// extraction kernel (Temporal Platform 10.0, C-2): regions JSON parsing,
// geometry building, and the streaming per-date reducer.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/temporal/temporal_region_table.h"

#include <json/json.h>

#include <cmath>
#include <limits>
#include <vector>

using namespace sicnu::temporal;
using Catch::Approx;

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

Json::Value parseJson( const std::string &text )
{
  Json::Value out;
  Json::CharReaderBuilder b;
  std::string errs;
  std::istringstream s( text );
  Json::parseFromStream( b, s, &out, &errs );
  return out;
}

/// North-up grid: origin (500000, 4500000), 30 m pixels, 100 × 100.
std::array<double, 6> testGeoTransform()
{
  return { 500000.0, 30.0, 0.0, 4500000.0, 0.0, -30.0 };
}
} // namespace

TEST_CASE( "parseRegionsJson: accepts points and polygons, rejects malformed input", "[temporal][regions]" )
{
  const Json::Value ok = parseJson( R"([
    {"id": "p1", "point": [500045.0, 4499985.0]},
    {"id": "f1", "polygon": [[500000.0, 4500000.0], [500300.0, 4500000.0],
                              [500300.0, 4499700.0], [500000.0, 4499700.0]]}
  ])" );
  QVector<RegionRef> regions;
  QString error;
  REQUIRE( parseRegionsJson( ok, 100, &regions, &error ) );
  REQUIRE( regions.size() == 2 );
  REQUIRE( regions[0].id == "p1" );
  REQUIRE( regions[0].isPoint );
  REQUIRE( regions[1].isPoint == false );
  REQUIRE( regions[1].polygon.size() == 4 );

  const QVector<RegionRef> none;
  // Duplicate ids are an error (never silently merged).
  const Json::Value dup = parseJson( R"([
    {"id": "a", "point": [1.0, 2.0]}, {"id": "a", "point": [3.0, 4.0]}
  ])" );
  REQUIRE( parseRegionsJson( dup, 100, &regions, &error ) == false );
  REQUIRE( error.contains( "duplicate" ) );

  // Both geometry kinds on one entry.
  const Json::Value both = parseJson( R"([
    {"id": "a", "point": [1.0, 2.0], "polygon": [[0,0],[1,0],[1,1]]}
  ])" );
  REQUIRE( parseRegionsJson( both, 100, &regions, &error ) == false );

  // max_regions guard.
  const Json::Value two = parseJson( R"([
    {"id": "a", "point": [1.0, 2.0]}, {"id": "b", "point": [3.0, 4.0]}
  ])" );
  REQUIRE( parseRegionsJson( two, 1, &regions, &error ) == false );
  REQUIRE( error.contains( "max_regions" ) );

  // Empty array refused.
  REQUIRE( parseRegionsJson( parseJson( "[]" ), 10, &regions, &error ) == false );
}

TEST_CASE( "buildRegionGeometry: points map to one pixel; outside points are refused", "[temporal][regions]" )
{
  const std::array<double, 6> gt = testGeoTransform();
  RegionRef point;
  point.id = "p";
  point.isPoint = true;
  point.x = 500045.0;  // col floor((500045-500000)/30) = 1
  point.y = 4499985.0; // row floor((4500000-4499985)/30) = 0

  RegionGeometry geom;
  QString error;
  REQUIRE( buildRegionGeometry( point, 0, gt, 100, 100, &geom, &error ) );
  REQUIRE( geom.xOff == 1 );
  REQUIRE( geom.yOff == 0 );
  REQUIRE( geom.w == 1 );
  REQUIRE( geom.h == 1 );
  REQUIRE( geom.insideCount() == 1 );

  point.x = 400000.0; // far outside
  REQUIRE( buildRegionGeometry( point, 0, gt, 100, 100, &geom, &error ) == false );
  REQUIRE( error.contains( "outside" ) );
}

TEST_CASE( "buildRegionGeometry: polygon membership uses pixel centers", "[temporal][regions]" )
{
  const std::array<double, 6> gt = testGeoTransform();
  RegionRef poly;
  poly.id = "f";
  poly.isPoint = false;
  // A 2×2-pixel window covering cols 1-2, rows 1-2.
  const double x0 = 500000.0 + 1 * 30.0;
  const double x1 = 500000.0 + 3 * 30.0;
  const double y0 = 4500000.0 - 1 * 30.0; // top
  const double y1 = 4500000.0 - 3 * 30.0; // bottom
  poly.polygon = { { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } } };

  RegionGeometry geom;
  QString error;
  REQUIRE( buildRegionGeometry( poly, 0, gt, 100, 100, &geom, &error ) );
  REQUIRE( geom.w == 2 );
  REQUIRE( geom.h == 2 );
  REQUIRE( geom.insideOffsets.size() == 4 ); // a 2×2 block: every center inside
  // Offsets are window-relative and sorted row-major.
  REQUIRE( geom.insideOffsets.front() == 0 );
  REQUIRE( geom.insideOffsets.back() == 3 );

  // A half-window polygon (left half only) keeps exactly one column.
  poly.polygon = { { { x0, y0 }, { x0 + 30.0, y0 }, { x0 + 30.0, y1 }, { x0, y1 } } };
  REQUIRE( buildRegionGeometry( poly, 0, gt, 100, 100, &geom, &error ) );
  REQUIRE( geom.insideOffsets.size() == 2 );
}

TEST_CASE( "RegionDateReducer: statistics and median budget degradation", "[temporal][regions]" )
{
  // Two regions: one with 4 pixels, one with 2.
  const std::vector<size_t> counts{ 4, 2 };
  RegionDateReducer reducer( 2, 64, counts );
  REQUIRE( reducer.medianEnabled() );

  reducer.beginDate();
  reducer.addSample( 0, 1.0f );
  reducer.addSample( 0, 2.0f );
  reducer.addSample( 0, 3.0f );
  reducer.addSample( 0, 10.0f );
  reducer.addSample( 1, 5.0f );
  reducer.addSample( 1, 7.0f );
  reducer.addSample( 1, kNan ); // never folded
  reducer.endDate();

  const auto &r0 = reducer.stats( 0 );
  REQUIRE( r0.validCount == 4 );
  REQUIRE( r0.mean == Approx( 4.0 ) );
  REQUIRE( r0.min == Approx( 1.0 ) );
  REQUIRE( r0.max == Approx( 10.0 ) );
  REQUIRE( r0.stddev == Approx( std::sqrt( ( 9.0 + 1.0 + 1.0 + 36.0 ) / 4.0 ) ) );
  REQUIRE( r0.median == Approx( 2.5f ) );

  const auto &r1 = reducer.stats( 1 );
  REQUIRE( r1.validCount == 2 );
  REQUIRE( r1.mean == Approx( 6.0 ) );
  REQUIRE( r1.median == Approx( 6.0f ) );
  REQUIRE( reducer.totalSamplesLastDate() == 6 );

  // A second date resets everything.
  reducer.beginDate();
  reducer.addSample( 0, 100.0f );
  reducer.endDate();
  REQUIRE( reducer.stats( 0 ).validCount == 1 );
  REQUIRE( reducer.stats( 0 ).mean == Approx( 100.0 ) );
  REQUIRE( reducer.stats( 1 ).validCount == 0 );
  REQUIRE( !std::isfinite( reducer.stats( 1 ).median ) );

  // Budget exceeded: the median degrades to NaN for every region; the count
  // stats stay exact.
  RegionDateReducer budgeted( 1, 2, { 4 } ); // 4 pixels > 2-float budget
  REQUIRE( budgeted.medianEnabled() == false );
  budgeted.beginDate();
  budgeted.addSample( 0, 1.0f );
  budgeted.addSample( 0, 2.0f );
  budgeted.endDate();
  REQUIRE( budgeted.stats( 0 ).validCount == 2 );
  REQUIRE( !std::isfinite( budgeted.stats( 0 ).median ) );
}
