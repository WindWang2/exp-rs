// test_terrain_foundation5.cpp — Foundation 5.0 Milestone F: curvature,
// multidirectional hillshade, local relief, and flow routing (closed forms
// + operator E2E).

#include "processing/algorithms/terrain_analysis.h"
#include "processing/algorithms/terrain_flow.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <cmath>
#include <limits>
#include <vector>

#include "synthetic_raster_builder.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace TerrainFlow;

namespace
{
constexpr float kNodata = -9999.0f;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
} // namespace

TEST_CASE( "Zevenbergen-Thorne curvatures on analytic surfaces", "[terrain][curvature]" )
{
  // Paraboloid z = ((x−2)² + (y−2)²)/2 on 5×5 unit cells: zxx = zyy = 1
  // everywhere, so profile = plan = total = +1 at every interior cell
  // (convexity-positive convention).
  constexpr int kN = 5;
  std::vector<float> dem( kN * kN );
  for ( int y = 0; y < kN; ++y )
    for ( int x = 0; x < kN; ++x )
      dem[static_cast<size_t>( y ) * kN + x] =
        static_cast<float>( 0.5 * ( ( x - 2 ) * ( x - 2 ) + ( y - 2 ) * ( y - 2 ) ) );

  std::vector<float> prof( kN * kN ), plan( kN * kN ), total( kN * kN );
  REQUIRE( TerrainAnalysis::curvatureProfile( dem.data(), prof.data(), kN, kN, 1, 1, kNodata ) );
  REQUIRE( TerrainAnalysis::curvaturePlan( dem.data(), plan.data(), kN, kN, 1, 1, kNodata ) );
  REQUIRE( TerrainAnalysis::curvatureTotal( dem.data(), total.data(), kN, kN, 1, 1, kNodata ) );

  const size_t centre = static_cast<size_t>( 2 ) * kN + 2;
  REQUIRE( total[centre] == Catch::Approx( 1.0f ).margin( 1e-6 ) );
  // The exact centre has zx = zy = 0 (flat) → profile/plan defined as 0.
  REQUIRE( prof[centre] == 0.0f );
  REQUIRE( plan[centre] == 0.0f );
  // Off-centre interior cells sit on the paraboloid: profile = plan = 1.
  const size_t p = static_cast<size_t>( 2 ) * kN + 1;
  REQUIRE( prof[p] == Catch::Approx( 1.0f ).margin( 1e-6 ) );
  REQUIRE( plan[p] == Catch::Approx( 1.0f ).margin( 1e-6 ) );

  // Cylinder z = (x−2)²: at (3,2) slope runs along x → profile = 2, plan = 0,
  // total = 1.
  std::vector<float> cyl( kN * kN );
  for ( int y = 0; y < kN; ++y )
    for ( int x = 0; x < kN; ++x )
      cyl[static_cast<size_t>( y ) * kN + x] = static_cast<float>( ( x - 2 ) * ( x - 2 ) );
  REQUIRE( TerrainAnalysis::curvatureProfile( cyl.data(), prof.data(), kN, kN, 1, 1, kNodata ) );
  REQUIRE( TerrainAnalysis::curvaturePlan( cyl.data(), plan.data(), kN, kN, 1, 1, kNodata ) );
  REQUIRE( TerrainAnalysis::curvatureTotal( cyl.data(), total.data(), kN, kN, 1, 1, kNodata ) );
  const size_t onSlope = static_cast<size_t>( 2 ) * kN + 3;
  REQUIRE( prof[onSlope] == Catch::Approx( 2.0f ).margin( 1e-6 ) );
  REQUIRE( plan[onSlope] == Catch::Approx( 0.0f ).margin( 1e-6 ) );
  REQUIRE( total[onSlope] == Catch::Approx( 1.0f ).margin( 1e-6 ) );
}

TEST_CASE( "Multidirectional hillshade is the 8-azimuth mean", "[terrain][hillshade]" )
{
  // Flat DEM: every azimuth shades the flat surface identically
  // (cos of the zenith = cos(45°) at 45° elevation).
  constexpr int kN = 4;
  std::vector<float> dem( kN * kN, 100.0f );
  std::vector<float> out( kN * kN );
  REQUIRE( TerrainAnalysis::hillshadeMultidirectional( dem.data(), out.data(), kN, kN, 1, 1,
                                                       kNodata, 45.0f ) );
  for ( const float v : out )
    REQUIRE( v == Catch::Approx( std::cos( 45.0 * 3.14159265358979 / 180.0 ) ).margin( 1e-5 ) );
}

TEST_CASE( "Local relief is the 3x3 max-min", "[terrain][relief]" )
{
  // 3×3 with a single peak of 10 over a 2-plane: centre relief = 8.
  std::vector<float> dem( 9, 2.0f );
  dem[4] = 10.0f;
  std::vector<float> out( 9 );
  REQUIRE( TerrainAnalysis::localRelief( dem.data(), out.data(), 3, 3, kNodata ) );
  REQUIRE( out[4] == Catch::Approx( 8.0f ) );
  // Corner cell (0): its raster-clipped window still covers (1,1) — the
  // peak — so its relief is 10 − 2 = 8.
  REQUIRE( out[0] == Catch::Approx( 8.0f ) );
}

TEST_CASE( "Priority-flood fill raises pits to their spill level", "[terrain][flow]" )
{
  // 3x5 valley: boundary rows 0/2 at 5, interior row carries a pit (3).
  // Every interior cell fills to 5 (the rim level of the boundary rows).
  constexpr int kW = 5;
  std::vector<float> dem( 3 * kW, 5.0f );
  const size_t pit = static_cast<size_t>( 1 ) * kW + 2;
  dem[static_cast<size_t>( 1 ) * kW + 1] = 4.0f;
  dem[pit] = 3.0f;
  dem[static_cast<size_t>( 1 ) * kW + 3] = 4.0f;
  std::vector<float> filled( 3 * kW );
  REQUIRE( fillDepressions( dem.data(), filled.data(), kW, 3, kNodata ) );
  REQUIRE( filled[pit] == Catch::Approx( 5.0f ) );
  REQUIRE( filled[static_cast<size_t>( 1 ) * kW + 1] == Catch::Approx( 5.0f ) );
  REQUIRE( filled[0] == Catch::Approx( 5.0f ) );

  // NoData column is a barrier: the east half cannot drain through it.
  // 5x5: col 0 = 5, col 1 = NoData (all rows), cols 2-3 = 5 with a pit at
  // (2,2), col 4 = 8; boundary rows carry the same pattern.
  constexpr int kN = 5;
  std::vector<float> bar( kN * kN, 5.0f );
  for ( int y = 0; y < kN; ++y )
  {
    bar[static_cast<size_t>( y ) * kN + 1] = kNodata;
    bar[static_cast<size_t>( y ) * kN + 4] = 8.0f;
  }
  bar[static_cast<size_t>( 2 ) * kN + 2] = 3.0f; // interior pit, west of the wall
  std::vector<float> filled2( kN * kN );
  REQUIRE( fillDepressions( bar.data(), filled2.data(), kN, kN, kNodata ) );
  REQUIRE( filled2[static_cast<size_t>( 2 ) * kN + 2] == Catch::Approx( 5.0f ) );
  REQUIRE( filled2[static_cast<size_t>( 2 ) * kN + 1] == kNodata );
  REQUIRE( filled2[static_cast<size_t>( 2 ) * kN + 4] == Catch::Approx( 8.0f ) );
  REQUIRE( filled2[0] == Catch::Approx( 5.0f ) );
}

TEST_CASE( "D8 direction and accumulation on a monotone slope", "[terrain][flow]" )
{
  // 5x1 slope [8,6,4,2,0] descends eastward: interior cells drain east
  // (code 1); the east boundary cell and the west boundary are sinks (0).
  // Accumulation (self-inclusive) accumulates at the west end: [4,3,2,1,1].
  std::vector<float> dem = { 8, 6, 4, 2, 0 };
  std::vector<float> filled( 5 ), dir( 5 ), acc( 5 );
  REQUIRE( fillDepressions( dem.data(), filled.data(), 5, 1, kNodata ) );
  REQUIRE( flowDirections( filled.data(), dir.data(), 5, 1, kNodata ) );
  REQUIRE( dir[0] == Catch::Approx( 1.0f ) ); // east
  REQUIRE( dir[1] == Catch::Approx( 1.0f ) ); // east
  REQUIRE( dir[3] == Catch::Approx( 1.0f ) ); // east
  REQUIRE( dir[4] == Catch::Approx( 0.0f ) ); // east edge: sink
  REQUIRE( flowAccumulation( dir.data(), acc.data(), 5, 1 ) );
  const float expected[5] = { 1, 2, 3, 4, 5 };
  for ( size_t i = 0; i < 5; ++i )
    REQUIRE( acc[i] == Catch::Approx( expected[i] ) );
}

TEST_CASE( "rs:terrain_flow E2E: accumulation over a slope DEM",
           "[terrain][flow][operator][e2e]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // 6×4 projected DEM, west-rising rows: z = 3·x (east edge drains west).
  constexpr int kW = 6;
  constexpr int kH = 4;
  RsSyntheticRasterBuilder b( kW, kH, 1 );
  b.withCrs( "EPSG:32650" );
  b.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
  for ( int y = 0; y < kH; ++y )
    for ( int x = 0; x < kW; ++x )
      b.withPixel( 1, x, y, static_cast<float>( 3 * x ) );
  const QString demPath = b.writeToDisk( dir.filePath( "dem.tif" ) );
  REQUIRE( !demPath.isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:terrain_flow" );
  REQUIRE( op != nullptr );

  RSOperatorContext context;
  Json::Value params( Json::objectValue );
  params["input"] = demPath.toStdString();
  params["output"] = dir.filePath( "acc.tif" ).toStdString();
  params["product"] = "flow_accumulation";
  REQUIRE_NOTHROW( op->run( params, context ) );

  GdalDatasetWrapper outDs;
  REQUIRE( outDs.open( dir.filePath( "acc.tif" ) ) );
  std::vector<float> acc( static_cast<size_t>( kW ) * kH );
  REQUIRE( outDs.readBandData( 1, acc.data(), kW, kH ) );
  // Every column drains west: acc[column] = kW − x for each row.
  for ( int y = 0; y < kH; ++y )
    for ( int x = 0; x < kW; ++x )
      REQUIRE( acc[static_cast<size_t>( y ) * kW + x] == Catch::Approx( kW - x ) );
}

TEST_CASE( "DEM flow accumulation preserves NoData sentinel",
           "[terrain][flow][nodata][issue783]" )
{
  constexpr float nodataVal = -9999.0f;
  constexpr int W = 4;
  constexpr int H = 4;
  std::vector<float> dem = {
      10.0f, 8.0f, nodataVal, nodataVal,
       9.0f, 7.0f, nodataVal, nodataVal,
       8.0f, 6.0f,      5.0f, nodataVal,
       7.0f, 5.0f,      4.0f,     3.0f
  };
  std::vector<float> filled( W * H, 0.0f );
  std::vector<float> dir( W * H, 0.0f );
  std::vector<float> acc( W * H, 0.0f );

  REQUIRE( TerrainFlow::fillDepressions( dem.data(), filled.data(), W, H, nodataVal ) );
  REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), W, H, nodataVal ) );
  REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), W, H ) );

  // NoData cells must retain the NoData sentinel and not become 1.0f ridges
  REQUIRE( dir[2] == nodataVal );
  REQUIRE( dir[3] == nodataVal );
  REQUIRE( dir[6] == nodataVal );
  REQUIRE( dir[7] == nodataVal );
  REQUIRE( dir[11] == nodataVal );

  REQUIRE( acc[2] == nodataVal );
  REQUIRE( acc[3] == nodataVal );
  REQUIRE( acc[6] == nodataVal );
  REQUIRE( acc[7] == nodataVal );
  REQUIRE( acc[11] == nodataVal );

  // Valid cells must have valid accumulation >= 1.0f
  REQUIRE( acc[0] >= 1.0f );
  REQUIRE( acc[1] >= 1.0f );
  REQUIRE( acc[4] >= 1.0f );
  REQUIRE( acc[5] >= 1.0f );
  REQUIRE( acc[15] >= 1.0f );
}

