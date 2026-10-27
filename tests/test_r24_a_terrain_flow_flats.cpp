// test_r24_a_terrain_flow_flats.cpp — R24-A science track (WP-A4).
//
// RED provenance (origin/master@5fe642ec6): the rs:terrain_flow operator's
// D8 products (flow_direction / flow_accumulation / watershed /
// stream_network / outlets) run fillDepressions → flowDirections on the
// filled surface, but flowDirections requires slope > 0 strictly, so every
// cell inside a filled depression (a flat plateau at spill level) becomes
// dir = 0 (a sink). The depression's contributing area therefore stops at
// the plateau edge and the spill outlet accumulates only its own cell —
// silently truncating the drainage network that the recommended
// "fill, then flow products" workflow is supposed to produce. The
// epsilon-gradient flat resolution (resolveFlats) already exists and is
// wired for flat_resolve / flow_direction_inf, but not for the D8 chain.
//
// Oracle (industry Fill+D8 semantics, cf. ArcGIS/TauDEM): a crater with a
// spill notch must route its whole bowl to the notch; the accumulation at
// and downstream of the notch includes the bowl's cell count.

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <cmath>
#include <vector>

using namespace sicnu::operators;

namespace
{

/// 11×11 crater: a 7×7 bowl (x,y ∈ [2,8]) at 0 m enclosed by a wall ring
/// (x,y ∈ [1,9]) at 10 m, one spill notch in the west wall at (row 5,
/// x=1) at 4 m; the x=0 column outside the wall sits at 2 m so the notch
/// drains further west. After depression filling the bowl becomes a 49-cell
/// plateau at 4 m — the flat the D8 chain must resolve toward the notch.
std::vector<float> craterDem()
{
    constexpr int kN = 11;
    std::vector<float> dem( static_cast<size_t>( kN ) * kN, 10.0f );
    for ( int y = 2; y <= 8; ++y )
        for ( int x = 2; x <= 8; ++x )
            dem[static_cast<size_t>( y ) * kN + x] = 0.0f; // bowl floor
    dem[static_cast<size_t>( 5 ) * kN + 1] = 4.0f;         // spill notch
    for ( int y = 0; y < kN; ++y )
        dem[static_cast<size_t>( y ) * kN + 0] = 2.0f;     // west outlet strip
    return dem;
}

std::vector<float> runFlowProduct( const QTemporaryDir &dir, const std::vector<float> &dem,
                                   const char *product, const char *outName,
                                   Json::Value *runResult = nullptr )
{
    using namespace sicnu::testing;
    constexpr int kN = 11;
    RsSyntheticRasterBuilder b( kN, kN, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kN ), -1.0 );
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
            b.withPixel( 1, x, y, dem[static_cast<size_t>( y ) * kN + x] );
    const QString demPath = b.writeToDisk( dir.filePath( "dem.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:terrain_flow" );
    REQUIRE( op != nullptr );
    RSOperatorContext context;
    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( outName ).toStdString();
    params["product"] = product;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    if ( runResult )
        *runResult = result;

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( dir.filePath( outName ) ) );
    std::vector<float> out( static_cast<size_t>( kN ) * kN );
    REQUIRE( ds.readBandData( 1, out.data(), kN, kN ) );
    return out;
}

} // namespace

TEST_CASE( "rs:terrain_flow accumulation routes a filled crater to its spill notch",
           "[processing][terrain][flow][r24_a]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kN = 11;
    Json::Value run;
    const std::vector<float> acc =
        runFlowProduct( dir, craterDem(), "flow_accumulation", "acc.tif", &run );
    // Observability fields (R2 review P3-2): the crater was resolved, and
    // the result must say so.
    CHECK( run["flatResolution"].asString() == "epsilon" );
    CHECK( run["flatEpsilon"].asDouble() > 0.0 );
    CHECK( run["raisedCells"].asUInt64() > 0 );

    // After filling, the 49-cell bowl is a plateau at 4 m whose only
    // escape is the notch (5,1). Every bowl cell plus the notch itself
    // must flow through it: accumulation at the notch >= 50. On master the
    // bowl is 49 sinks and the notch carries only itself (1).
    const float atNotch = acc[static_cast<size_t>( 5 ) * kN + 1];
    CHECK( atNotch >= 50.0f );
    // The outlet strip cell west of the notch carries the bowl onward.
    const float downstream = acc[static_cast<size_t>( 5 ) * kN + 0];
    CHECK( downstream >= atNotch + 1.0f );
    // A bowl interior cell has at least its own row-east neighbourhood
    // draining into it under the epsilon gradient (>= 2 incl. self).
    const float bowlCentre = acc[static_cast<size_t>( 5 ) * kN + 5];
    CHECK( bowlCentre >= 2.0f );
}

TEST_CASE( "rs:terrain_flow flow_direction resolves filled-flat cells",
           "[processing][terrain][flow][r24_a]" )
{
    // ESRI D8 codes; 0 = sink. On master the 49 filled-bowl cells are all
    // sinks (slope 0 to every neighbour); after flat resolution none of
    // the bowl interior may stay 0.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kN = 11;
    const std::vector<float> dirs =
        runFlowProduct( dir, craterDem(), "flow_direction", "dir.tif" );

    int sinksInBowl = 0;
    for ( int y = 2; y <= 8; ++y )
        for ( int x = 2; x <= 8; ++x )
            sinksInBowl += ( dirs[static_cast<size_t>( y ) * kN + x] == 0.0f );
    CHECK( sinksInBowl == 0 );
}

TEST_CASE( "rs:terrain_flow flat against NoData stays a sink, not fabricated flow",
           "[processing][terrain][flow][r24_a]" )
{
    // Negative control (R1 review P3-7): a 3×3 flat plateau enclosed by
    // NoData has no escape — the resolution contract never raises cells to
    // route into NoData, so every plateau cell must REMAIN dir = 0 (a true
    // sink) rather than fabricating an outlet. Pins the documented
    // resolveFlats boundary semantics so a future change cannot quietly
    // invent drainage into NoData.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kN = 5;
    std::vector<float> dem( static_cast<size_t>( kN ) * kN, -9999.0f );
    for ( int y = 1; y <= 3; ++y )
        for ( int x = 1; x <= 3; ++x )
            dem[static_cast<size_t>( y ) * kN + x] = 10.0f; // flat island

    using namespace sicnu::testing;
    RsSyntheticRasterBuilder b( kN, kN, 1 );
    b.withNoData( -9999.0 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kN ), -1.0 );
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
            b.withPixel( 1, x, y, dem[static_cast<size_t>( y ) * kN + x] );
    const QString demPath = b.writeToDisk( dir.filePath( "island.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:terrain_flow" );
    REQUIRE( op != nullptr );
    RSOperatorContext context;
    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "dir.tif" ).toStdString();
    params["product"] = "flow_direction";
    REQUIRE_NOTHROW( op->run( params, context ) );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( dir.filePath( "dir.tif" ) ) );
    std::vector<float> dirs( static_cast<size_t>( kN ) * kN );
    REQUIRE( ds.readBandData( 1, dirs.data(), kN, kN ) );
    // resolveFlats' NoData contract (terrain_hydrology.cpp seeding): the
    // drain-boundary seeds — every valid cell adjacent to NoData — are
    // never raised, so the 8 island border cells remain dir = 0 sinks;
    // flow ends AT the NoData border, never fabricated into it. The
    // interior cell receives an epsilon gradient toward the border and may
    // route (its exact direction is an implementation detail of the
    // epsilon descent; the pin is that drainage terminates on the border).
    for ( int y = 1; y <= 3; ++y )
        for ( int x = 1; x <= 3; ++x )
        {
            if ( x == 2 && y == 2 )
                continue; // interior: epsilon descent toward the border
            INFO( "island border cell (" << x << "," << y << ") dir="
                                         << dirs[static_cast<size_t>( y ) * kN + x] );
            CHECK( dirs[static_cast<size_t>( y ) * kN + x] == 0.0f );
        }
}

TEST_CASE( "rs:terrain_flow flat-free surface reports flatResolution none",
           "[processing][terrain][flow][r24_a]" )
{
    // The gate's false-negative face (R2 review P3-2): a strictly monotone
    // DEM has no equal-elevation neighbours, so no flat resolution runs and
    // the result must say "none" — and the accumulation still matches the
    // plain chain (the slope oracle from test_terrain_foundation5).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kN = 11;
    std::vector<float> slope( static_cast<size_t>( kN ) * kN );
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
            slope[static_cast<size_t>( y ) * kN + x] = static_cast<float>( 3 * x + y );
    Json::Value run;
    const std::vector<float> acc =
        runFlowProduct( dir, slope, "flow_accumulation", "acc.tif", &run );
    CHECK( run["flatResolution"].asString() == "none" );
    CHECK( run.isMember( "raisedCells" ) == false );
    // D8 oracle for z = 3x + y: every interior cell drains due WEST (slope
    // 3 beats every other direction), so row x gets kN - x; the west
    // column then drains NORTH (the only descent left) and funnels the
    // whole grid into (0,0): column cell y carries 11 * (11 - y).
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
        {
            const float expected = ( x == 0 ) ? static_cast<float>( kN * ( kN - y ) )
                                              : static_cast<float>( kN - x );
            CHECK( acc[static_cast<size_t>( y ) * kN + x]
                   == Catch::Approx( expected ).margin( 1e-6f ) );
        }
}
