// tests/test_mosaic_plan.cpp — F15 Package A oracle tests.
//
// Independent truths: hand-computed grid geometry (union extent, offsets,
// overlap areas) and the documented PROJ value for EPSG:4326 -> EPSG:32631
// at lon=0/lat=0 (x = 166021.4430 m at the equator). Nothing here derives
// expectations from the implementation under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/mosaic_plan.h"

#include <ogr_spatialref.h>

#include <cmath>
#include <string>
#include <vector>

using rs::mosaic::MosaicPlanner;
using rs::mosaic::MosaicPlan;
using rs::mosaic::SceneEntry;
using Catch::Approx;

namespace {

SceneEntry northUpScene( const std::string &path, double originX, double originY,
                         int w, int h, double px = 1.0, int priority = 0 )
{
    SceneEntry s;
    s.path = path;
    s.width = w;
    s.height = h;
    s.geoTransform = { originX, px, 0.0, originY, 0.0, -px };
    s.bandCount = 1;
    s.priority = priority;
    return s;
}

std::string wktFor( const char *userInput )
{
    OGRSpatialReference srs;
    REQUIRE( srs.SetFromUserInput( userInput ) == OGRERR_NONE );
    char *wkt = nullptr;
    REQUIRE( srs.exportToWkt( &wkt ) == OGRERR_NONE );
    std::string out( wkt );
    CPLFree( wkt );
    return out;
}

} // namespace

TEST_CASE( "Plan: two aligned overlapping scenes -> union grid, offsets, overlap 25",
           "[processing][mosaic][plan]" )
{
    std::vector<SceneEntry> scenes;
    scenes.push_back( northUpScene( "a.tif", 0.0, 0.0, 10, 10 ) );   // (0,0)-(10,-10)
    scenes.push_back( northUpScene( "b.tif", 5.0, -5.0, 10, 10 ) );  // (5,-5)-(15,-15)

    MosaicPlanner::Options opt;
    const auto plan = MosaicPlanner::build( scenes, opt );
    REQUIRE( plan.has_value() );

    CHECK( plan->width == 15 );
    CHECK( plan->height == 15 );
    CHECK( plan->gridTransform[0] == Approx( 0.0 ) );
    CHECK( plan->gridTransform[3] == Approx( 0.0 ) ); // north-up: origin at max Y
    CHECK( plan->gridTransform[5] == Approx( -1.0 ) );

    REQUIRE( plan->scenes.size() == 2 );
    CHECK( plan->scenes[0].placement.offsetX == 0 );
    CHECK( plan->scenes[0].placement.offsetY == 0 );
    CHECK( plan->scenes[1].placement.offsetX == 5 );
    CHECK( plan->scenes[1].placement.offsetY == 5 );
    CHECK( plan->scenes[0].placement.aligned );
    CHECK( plan->scenes[1].placement.aligned );

    // Same priority -> input order: scene 0 painted first, scene 1 on top.
    REQUIRE( plan->compositeOrder.size() == 2 );
    CHECK( plan->compositeOrder[0] == 0 );
    CHECK( plan->compositeOrder[1] == 1 );

    REQUIRE( plan->overlaps.size() == 1 );
    CHECK( plan->overlaps[0].a == 0 );
    CHECK( plan->overlaps[0].b == 1 );
    CHECK( plan->overlaps[0].pixels == 25 );

    CHECK( plan->diagnostics.warnings.empty() );
}

TEST_CASE( "Plan: priority drives composite order, not input order",
           "[processing][mosaic][plan]" )
{
    std::vector<SceneEntry> scenes;
    scenes.push_back( northUpScene( "low.tif", 0.0, 0.0, 10, 10, 1.0, /*priority=*/1 ) );
    scenes.push_back( northUpScene( "high.tif", 0.0, 0.0, 10, 10, 1.0, /*priority=*/5 ) );

    MosaicPlanner::Options opt;
    const auto plan = MosaicPlanner::build( scenes, opt );
    REQUIRE( plan.has_value() );
    REQUIRE( plan->compositeOrder.size() == 2 );
    CHECK( plan->compositeOrder[0] == 0 ); // painted first (below)
    CHECK( plan->compositeOrder[1] == 1 ); // painted last (on top)
    CHECK( plan->overlaps.size() == 1 );
    CHECK( plan->overlaps[0].pixels == 100 );
}

TEST_CASE( "Plan: disjoint scenes yield no overlaps but union extent",
           "[processing][mosaic][plan]" )
{
    std::vector<SceneEntry> scenes;
    scenes.push_back( northUpScene( "left.tif", 0.0, 0.0, 10, 10 ) );
    scenes.push_back( northUpScene( "right.tif", 10.0, 0.0, 10, 10 ) );

    MosaicPlanner::Options opt;
    const auto plan = MosaicPlanner::build( scenes, opt );
    REQUIRE( plan.has_value() );
    CHECK( plan->width == 20 );
    CHECK( plan->height == 10 );
    CHECK( plan->overlaps.empty() );
}

TEST_CASE( "Plan: sub-pixel offset is flagged with fractional residual",
           "[processing][mosaic][plan]" )
{
    std::vector<SceneEntry> scenes;
    scenes.push_back( northUpScene( "a.tif", 0.0, 0.0, 10, 10 ) );
    scenes.push_back( northUpScene( "b.tif", 5.4, -5.0, 10, 10 ) );

    MosaicPlanner::Options opt;
    const auto plan = MosaicPlanner::build( scenes, opt );
    REQUIRE( plan.has_value() );

    CHECK( plan->scenes[0].placement.aligned );
    CHECK_FALSE( plan->scenes[1].placement.aligned );
    CHECK( plan->scenes[1].placement.fracOffsetX == Approx( 0.4 ).margin( 1e-9 ) );
    REQUIRE( plan->diagnostics.subPixelOffset.size() == 1 );
    CHECK( plan->diagnostics.subPixelOffset[0] == 1 );
    REQUIRE( plan->diagnostics.warnings.size() == 1 );
    CHECK( plan->diagnostics.warnings[0].find( "sub-pixel" ) != std::string::npos );
}

TEST_CASE( "Plan: rotated raster is diagnosed and grid-ineligible",
           "[processing][mosaic][plan]" )
{
    SceneEntry a = northUpScene( "a.tif", 0.0, 0.0, 10, 10 );
    SceneEntry b = northUpScene( "rot.tif", 0.0, 0.0, 10, 10 );
    b.geoTransform[2] = 0.05; // shear

    MosaicPlanner::Options opt;
    const auto plan = MosaicPlanner::build( { a, b }, opt );
    REQUIRE( plan.has_value() );

    REQUIRE( plan->diagnostics.rotated.size() == 1 );
    CHECK( plan->diagnostics.rotated[0] == 1 );
    CHECK_FALSE( plan->scenes[1].gridEligible );
    CHECK( plan->scenes[1].placement.valid == false );
    CHECK( plan->overlaps.empty() ); // rotated scene never participates
    CHECK( plan->width == 10 );      // extent from eligible scene only
}

TEST_CASE( "Plan: mixed CRS is diagnosed with transformed footprint envelope",
           "[processing][mosaic][plan]" )
{
    SceneEntry a = northUpScene( "deg.tif", 0.0, 0.0, 10, 10 );
    a.crsWkt = wktFor( "EPSG:4326" );

    // A metric scene far away in EPSG:32631 — its footprint transformed into
    // EPSG:4326 must land near lon=0, lat=0 (documented PROJ behaviour for
    // the 32631 western edge: x=166021.4430 m at the equator -> lon ~ 0).
    SceneEntry b = northUpScene( "utm.tif", 166021.4430, 0.0, 10, 10 );
    b.crsWkt = wktFor( "EPSG:32631" );

    MosaicPlanner::Options opt;
    const auto plan = MosaicPlanner::build( { a, b }, opt );
    REQUIRE( plan.has_value() );
    CHECK( plan->crsWkt == a.crsWkt );
    REQUIRE( plan->diagnostics.crsMismatch.size() == 1 );
    CHECK( plan->diagnostics.crsMismatch[0] == 1 );
    CHECK_FALSE( plan->scenes[1].gridEligible );

    // Independent footprint oracle: transform b's own envelope into EPSG:4326.
    const auto env = MosaicPlanner::footprintEnvelope( b, "EPSG:4326" );
    REQUIRE( env.has_value() );
    CHECK( ( *env )[0] == Approx( 0.0 ).margin( 1e-4 ) ); // minX ~ lon 0
    CHECK( ( *env )[2] == Approx( 0.0 ).margin( 1e-4 ) ); // maxX ~ lon 0
    CHECK( ( *env )[1] == Approx( 0.0 ).margin( 1e-4 ) );
    CHECK( ( *env )[3] > 0.0 );
    CHECK( plan->diagnostics.warnings[0].find( "reproject" ) != std::string::npos );
}

TEST_CASE( "Plan: south-up reference produces a south-up grid",
           "[processing][mosaic][plan]" )
{
    SceneEntry a;
    a.path = "s0.tif";
    a.width = 10;
    a.height = 10;
    a.geoTransform = { 0.0, 1.0, 0.0, 10.0, 0.0, 1.0 }; // south-up, y grows downward-in-south
    SceneEntry b;
    b.path = "s1.tif";
    b.width = 10;
    b.height = 10;
    b.geoTransform = { 5.0, 1.0, 0.0, 15.0, 0.0, 1.0 };

    MosaicPlanner::Options opt;
    const auto plan = MosaicPlanner::build( { a, b }, opt );
    REQUIRE( plan.has_value() );
    CHECK( plan->gridTransform[5] == Approx( 1.0 ) );
    CHECK( plan->gridTransform[3] == Approx( 10.0 ) ); // south-up origin at min Y
    CHECK( plan->height == 15 );
    CHECK( plan->scenes[0].placement.offsetY == 0 );
    CHECK( plan->scenes[1].placement.offsetY == 5 );
    CHECK( plan->overlaps.size() == 1 );
    CHECK( plan->overlaps[0].pixels == 25 );
}

TEST_CASE( "Plan: negative cases fail closed with actionable messages",
           "[processing][mosaic][plan]" )
{
    MosaicPlanner::Options opt;
    std::string err;

    CHECK_FALSE( MosaicPlanner::build( {}, opt, &err ).has_value() );
    CHECK( err.find( "no scenes" ) != std::string::npos );

    std::vector<SceneEntry> zeroPx = { northUpScene( "z.tif", 0, 0, 10, 10 ) };
    zeroPx[0].geoTransform[1] = 0.0;
    CHECK_FALSE( MosaicPlanner::build( zeroPx, opt, &err ).has_value() );
    CHECK( err.find( "zero pixel size" ) != std::string::npos );

    std::vector<SceneEntry> zeroW = { northUpScene( "z.tif", 0, 0, 0, 10 ) };
    CHECK_FALSE( MosaicPlanner::build( zeroW, opt, &err ).has_value() );

    std::vector<SceneEntry> nanGt = { northUpScene( "n.tif", 0, 0, 10, 10 ) };
    nanGt[0].geoTransform[0] = std::nan( "" );
    CHECK_FALSE( MosaicPlanner::build( nanGt, opt, &err ).has_value() );
    CHECK( err.find( "non-finite" ) != std::string::npos );

    opt.referenceIndex = 7;
    std::vector<SceneEntry> two = { northUpScene( "a.tif", 0, 0, 4, 4 ),
                                    northUpScene( "b.tif", 2, 0, 4, 4 ) };
    CHECK_FALSE( MosaicPlanner::build( two, opt, &err ).has_value() );
    CHECK( err.find( "reference index" ) != std::string::npos );

    // No grid-eligible scene: the candidate reference is rotated (ineligible
    // by geometry) and the other scene is in a different CRS (ineligible by
    // reprojection requirement).
    opt.referenceIndex = -1;
    SceneEntry deg = northUpScene( "deg.tif", 0, 0, 10, 10 );
    deg.crsWkt = wktFor( "EPSG:4326" );
    deg.geoTransform[2] = 0.05; // rotated -> grid-ineligible
    SceneEntry utm = northUpScene( "utm.tif", 166021.0, 0.0, 10, 10 );
    utm.crsWkt = wktFor( "EPSG:32731" ); // different CRS -> grid-ineligible
    CHECK_FALSE( MosaicPlanner::build( { deg, utm }, opt, &err ).has_value() );
    CHECK( err.find( "no grid-eligible" ) != std::string::npos );
}

TEST_CASE( "Plan: sameCrs handles string equality, WKT equivalence and difference",
           "[processing][mosaic][plan]" )
{
    const std::string wgs84 = wktFor( "EPSG:4326" );
    CHECK( MosaicPlanner::sameCrs( wgs84, wgs84 ) );
    CHECK( MosaicPlanner::sameCrs( "EPSG:4326", wgs84 ) );
    CHECK_FALSE( MosaicPlanner::sameCrs( "EPSG:4326", "EPSG:3857" ) );
    CHECK_FALSE( MosaicPlanner::sameCrs( "EPSG:4326", "" ) );
}
