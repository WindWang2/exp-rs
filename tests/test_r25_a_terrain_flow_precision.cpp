// test_r25_a_terrain_flow_precision.cpp — R25-A DEF-2: D8 flow accumulation
// must count cells exactly. The float32 accumulator lost exactness once a
// basin exceeded 2^24 cells (float32 cannot represent odd integers above
// 2^24): a 4097×4097 all-to-one-corner funnel reported 16785408 instead of
// 16785409 at the outlet, and the rounding compounded along the routing
// order. The fix accumulates in uint64 internally and publishes the
// flow_accumulation product as a Float64 raster.
//
// Also pins the metamorphic invariants that must NOT change:
//   * small grids (≤2^24) stay float-exact in the classic float API;
//   * NoData cells keep the sentinel in the float API (#783);
//   * mass identity: outlet count == number of valid cells.

#include "synthetic_raster_builder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/terrain_flow.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <gdal.h>
#include <json/json.h>

#include <cmath>
#include <limits>
#include <cstdint>
#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{

struct RunResult
{
    Json::Value json;
    bool ok = false;
    RSOperatorError error = RSOperatorError( ErrorCode::Unknown, "" );
};

RunResult runOperator( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    RunResult run;
    if ( op == nullptr )
    {
        run.error = RSOperatorError( ErrorCode::InvalidParameter, "operator not registered" );
        return run;
    }
    RSOperatorContext context;
    try
    {
        run.json = op->run( params, context );
        run.ok = true;
    }
    catch ( const RSOperatorError &e )
    {
        run.error = e;
    }
    return run;
}

// Direction grid of a funnel: every cell flows EAST; the eastern rim flows
// NORTH; the north-east corner is the sink. Every cell drains into the
// corner, so counts are exactly predictable: corner = W*H, the cell one step
// west of the corner on row 0 = W (its row prefix), etc.
std::vector<float> funnelDirs( int width, int height )
{
    std::vector<float> dir( static_cast<size_t>( width ) * height, 0.0f );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * width + x;
            if ( x == width - 1 && y == 0 )
                dir[i] = 0.0f; // the sink
            else if ( x == width - 1 )
                dir[i] = 64.0f; // N
            else
                dir[i] = 1.0f; // E
        }
    return dir;
}

} // namespace

TEST_CASE( "D8 accumulation counts a >2^24 funnel exactly",
           "[terrain][flow][r25a]" )
{
    // 4097×4097 = 16,785,409 cells (odd) — one above the float32 integer
    // range 2^24 = 16,777,216, so the exact count is NOT representable in
    // float32 while every intermediate is an integer.
    constexpr int kSide = 4097;
    const std::vector<float> dir = funnelDirs( kSide, kSide );

    std::vector<std::uint64_t> counts;
    REQUIRE( TerrainFlow::flowAccumulationCounts( dir.data(), kSide, kSide,
                                                  nullptr, 0.0f, &counts ) );
    const std::uint64_t expected =
        static_cast<std::uint64_t>( kSide ) * static_cast<std::uint64_t>( kSide );
    // The corner sink (W-1, 0) receives every cell of the funnel.
    CHECK( counts[static_cast<size_t>( kSide - 1 )] == expected );
    // Per-cell oracle: cell (x, 0) on the top row carries x+1 cells (its
    // row prefix); interior rows feed the eastern rim, which chains north
    // into the corner. Row r's rim cell (W-1, r) carries (H-r)·W cells.
    CHECK( counts[0] == 1 );
    CHECK( counts[1] == 2 );
    CHECK( counts[2] == 3 );
    CHECK( counts[static_cast<size_t>( 1 ) * kSide + static_cast<size_t>( kSide - 1 )]
          == expected - static_cast<std::uint64_t>( kSide ) );
}

TEST_CASE( "D8 float API stays exact below 2^24 and keeps sentinels",
           "[terrain][flow][r25a]" )
{
    constexpr int kSide = 512;
    const std::vector<float> dir = funnelDirs( kSide, kSide );
    std::vector<float> acc( static_cast<size_t>( kSide ) * kSide, 0.0f );
    REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), kSide, kSide ) );
    const float expectedCorner = static_cast<float>(
        static_cast<std::uint64_t>( kSide ) * static_cast<std::uint64_t>( kSide ) );
    CHECK( acc[static_cast<size_t>( kSide - 1 )] == expectedCorner ); // 262144
    CHECK( acc[0] == 1.0f );
    CHECK( acc[1] == 2.0f );

    // Masked overload: a NoData wall splits the funnel; masked cells carry
    // the sentinel (#783), not a count.
    std::vector<float> masked = dir;
    constexpr int kWallX = 100; // vertical NoData wall
    for ( int y = 0; y < kSide; ++y )
        masked[static_cast<size_t>( y ) * kSide + kWallX] = std::nanf( "" );
    std::vector<float> filled( masked.size(), 0.0f );
    for ( size_t i = 0; i < filled.size(); ++i )
        filled[i] = std::isnan( masked[i] ) ? std::nanf( "" ) : 1.0f;
    const float nodata = std::nanf( "" );
    std::vector<float> acc2( masked.size(), 0.0f );
    REQUIRE( TerrainFlow::flowAccumulation( masked.data(), acc2.data(), kSide, kSide,
                                            filled.data(), nodata ) );
    CHECK( std::isnan( acc2[static_cast<size_t>( 0 ) * kSide + kWallX] ) );
    // West of the wall nothing crosses: the wall-adjacent western cells
    // keep their small row-prefix counts.
    CHECK( acc2[static_cast<size_t>( 0 ) * kSide + kWallX - 1] == 100.0f );
}

TEST_CASE( "rs:terrain_flow flow_accumulation publishes Float64 exact counts",
           "[terrain][flow][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // A tilted plane z = x + y: the NW diagonal is steepest, so every cell
    // chains into the origin (0,0). 300×300 = 90k cells is well below 2^24;
    // the operator E2E checks dtype + exactness plumbing (the >2^24
    // magnitude is covered at kernel level above to keep the E2E
    // affordable).
    constexpr int kSide = 300;
    RsSyntheticRasterBuilder b( kSide, kSide, 1 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kSide ), -1.0 );
    for ( int y = 0; y < kSide; ++y )
        for ( int x = 0; x < kSide; ++x )
            b.withPixel( 1, x, y, static_cast<float>( x + y ) );
    const QString demPath = b.writeToDisk( dir.filePath( "plane.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "accum.tif" ).toStdString();
    params["product"] = "flow_accumulation";
    const RunResult run = runOperator( "rs:terrain_flow", params );
    REQUIRE( run.ok );

    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "accum.tif" ) ) );
    CHECK( outDs.bandDataType( 1 ) == GDT_Float64 ); // RED on master: Float32
    CHECK( run.json["maxAccumulation"].asUInt64()
           == static_cast<std::uint64_t>( kSide ) * kSide );
    std::vector<double> accum( static_cast<size_t>( kSide ) * kSide );
    REQUIRE( outDs.readBandDataNative( 1, accum.data(), kSide, kSide ) );
    // z = x + y: the NW diagonal is the steepest descent (drop 2 / √2), so
    // interior cells chain diagonally and the rims chain along the edges —
    // every cell drains into the origin (0,0), whose exact count equals
    // the grid size.
    CHECK( accum[0] == static_cast<double>( kSide ) * kSide );
}

TEST_CASE( "flow_accumulation keeps the NaN sentinel without declared NoData",
           "[terrain][flow][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Tilted plane with a NaN wall down column 4 (no declared nodata — NaN
    // is the marker). The Float64 product must carry NaN at those cells
    // (#783 spirit), never a phantom 0.0 count (R1-3).
    constexpr int kSide = 32;
    RsSyntheticRasterBuilder b( kSide, kSide, 1, GDT_Float32 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kSide ), -1.0 );
    for ( int y = 0; y < kSide; ++y )
        for ( int x = 0; x < kSide; ++x )
            b.withPixel( 1, x, y,
                         x == 4 ? std::numeric_limits<float>::quiet_NaN()
                                : static_cast<float>( x + y ) );
    const QString demPath = b.writeToDisk( dir.filePath( "nanwall.tif" ) );
    REQUIRE( !demPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = demPath.toStdString();
    params["output"] = dir.filePath( "accum_nan.tif" ).toStdString();
    params["product"] = "flow_accumulation";
    const RunResult run = runOperator( "rs:terrain_flow", params );
    REQUIRE( run.ok );
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "accum_nan.tif" ) ) );
    CHECK( outDs.bandDataType( 1 ) == GDT_Float64 );
    std::vector<double> accum( static_cast<size_t>( kSide ) * kSide );
    REQUIRE( outDs.readBandDataNative( 1, accum.data(), kSide, kSide ) );
    for ( int y = 0; y < kSide; ++y )
    {
        INFO( "row " << y );
        CHECK( std::isnan( accum[static_cast<size_t>( y ) * kSide + 4] ) );
        // West of the wall drains within its own block; east of the wall
        // likewise — finite, exact counts everywhere else.
        CHECK( std::isfinite( accum[static_cast<size_t>( y ) * kSide + 0] ) );
        CHECK( std::isfinite( accum[static_cast<size_t>( y ) * kSide + 5] ) );
    }
}
