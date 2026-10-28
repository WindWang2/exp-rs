// test_r25_a_perf_baseline.cpp — R25-A W5 performance baselines.
//
// NOT a wall-clock CI gate (per repo discipline): these cases measure and
// REPORT. Each hotspot runs 5 times on a medium synthetic raster; the test
// prints the median throughput (Mpx/s) and the process high-water RSS
// (VmHWM) so the numbers can be compared across revisions. The only hard
// assertion is functional correctness of the measured run (the product
// matches the small-grid oracle), so a slow shared host cannot fail the
// lane while a corrupted product still can.
//
// Baselines (R25-A):
//   1. rs:terrain_flow flow_accumulation (2048x2048, uint64-exact path)
//   2. rs:spectral_resample (50 -> 3 bands, 1024x1024)
//   3. rs:post_classification_change (1024x1024, 4 classes)

#include "synthetic_raster_builder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <json/json.h>
#include <string>
#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{

Json::Value runOp( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    RSOperatorContext context;
    return op->run( params, context ); // throws RSOperatorError on failure
}

/// Process high-water RSS in MiB (Linux /proc).
double peakRssMiB()
{
    std::ifstream status( "/proc/self/status" );
    std::string key;
    long value = 0;
    std::string unit;
    while ( status >> key )
    {
        if ( key == "VmHWM:" )
        {
            status >> value >> unit;
            return static_cast<double>( value ) / 1024.0;
        }
        status.ignore( 4096, '\n' );
    }
    return -1.0;
}

double medianOf( std::vector<double> &v )
{
    std::sort( v.begin(), v.end() );
    return v[v.size() / 2];
}

void report( const char *what, std::vector<double> &mpxPerSec, double rssBefore,
             double rssAfter )
{
    const double median = medianOf( mpxPerSec );
    std::printf( "[R25A-PERF] %-38s median %.2f Mpx/s over %zu runs | VmHWM %.1f -> %.1f MiB\n",
                 what, median, mpxPerSec.size(), rssBefore, rssAfter );
    for ( double v : mpxPerSec )
        std::printf( "[R25A-PERF]   run: %.3f Mpx/s\n", v );
}

} // namespace

TEST_CASE( "perf baseline: terrain flow accumulation 2048x2048",
           "[perf][r25a][baseline]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kSide = 2048;
    RsSyntheticRasterBuilder b( kSide, kSide, 1, GDT_Float32 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kSide ), -1.0 );
    // Plane z = x + y: NW-steepest drainage into the origin (see the
    // precision test); every cell valid, so throughput measures the pure
    // fill + route + accumulate path.
    for ( int y = 0; y < kSide; ++y )
        for ( int x = 0; x < kSide; ++x )
            b.withPixel( 1, x, y, static_cast<float>( x + y ) );
    const QString dem = b.writeToDisk( dir.filePath( "plane.tif" ) );
    REQUIRE( !dem.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = dem.toStdString();
    params["output"] = dir.filePath( "accum.tif" ).toStdString();
    params["product"] = "flow_accumulation";

    const double rssBefore = peakRssMiB();
    std::vector<double> mpx;
    for ( int run = 0; run < 5; ++run )
    {
        params["output"] = dir.filePath( QStringLiteral( "accum%1.tif" ).arg( run ) )
                               .toStdString();
        const auto t0 = std::chrono::steady_clock::now();
        const Json::Value result = runOp( "rs:terrain_flow", params );
        const auto t1 = std::chrono::steady_clock::now();
        // Correctness oracle on every measured run: the origin drains the
        // whole grid exactly (DEF-2 exactness) — the perf run is also a
        // repetition of the GREEN check.
        REQUIRE( result["maxAccumulation"].asUInt64()
                 == static_cast<unsigned long long>( kSide ) * kSide );
        mpx.push_back( static_cast<double>( kSide ) * kSide / 1.0e6
                       / std::chrono::duration<double>( t1 - t0 ).count() );
    }
    report( "terrain flow_accumulation", mpx, rssBefore, peakRssMiB() );
    CHECK( medianOf( mpx ) > 0.0 );
}

TEST_CASE( "perf baseline: spectral resample 1024x1024 50->3 bands",
           "[perf][r25a][baseline]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kSide = 1024;
    constexpr int kBands = 50;
    RsSyntheticRasterBuilder b( kSide, kSide, kBands, GDT_Float32 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kSide ), -1.0 );
    for ( int band = 1; band <= kBands; ++band )
        for ( int y = 0; y < kSide; y += 1 )
            for ( int x = 0; x < kSide; x += 1 )
                b.withPixel( band, x, y, static_cast<float>( band ) );
    const QString cube = b.writeToDisk( dir.filePath( "cube.tif" ) );
    REQUIRE( !cube.isEmpty() );

    Json::Value params( Json::objectValue );
    params["input"] = cube.toStdString();
    params["wavelengths"] = Json::Value( Json::arrayValue );
    params["wavelengths"].append( 500.0 );
    params["wavelengths"].append( 600.0 );
    params["wavelengths"].append( 700.0 );

    const double rssBefore = peakRssMiB();
    std::vector<double> mpx;
    for ( int run = 0; run < 5; ++run )
    {
        params["output"] = dir.filePath( QStringLiteral( "res%1.tif" ).arg( run ) )
                               .toStdString();
        const auto t0 = std::chrono::steady_clock::now();
        const Json::Value result = runOp( "rs:spectral_resample", params );
        const auto t1 = std::chrono::steady_clock::now();
        REQUIRE( result["bands"].asInt() == 3 );
        mpx.push_back( static_cast<double>( kSide ) * kSide / 1.0e6
                       / std::chrono::duration<double>( t1 - t0 ).count() );
    }
    report( "spectral resample 50->3", mpx, rssBefore, peakRssMiB() );
    CHECK( medianOf( mpx ) > 0.0 );
}

TEST_CASE( "perf baseline: post-classification change 1024x1024",
           "[perf][r25a][baseline]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int kSide = 1024;
    RsSyntheticRasterBuilder before( kSide, kSide, 1, GDT_Byte );
    before.withCrs( "EPSG:32650" );
    before.withGeoTransform( 0.0, 1.0, static_cast<double>( kSide ), -1.0 );
    RsSyntheticRasterBuilder after( kSide, kSide, 1, GDT_Byte );
    after.withCrs( "EPSG:32650" );
    after.withGeoTransform( 0.0, 1.0, static_cast<double>( kSide ), -1.0 );
    for ( int y = 0; y < kSide; ++y )
        for ( int x = 0; x < kSide; ++x )
        {
            const float cls = static_cast<float>( ( x / 64 + y / 64 ) % 4 );
            before.withPixel( 1, x, y, cls );
            after.withPixel( 1, x, y, static_cast<float>( ( static_cast<int>( cls ) + 1 ) % 4 ) );
        }
    const QString beforePath = before.writeToDisk( dir.filePath( "before.tif" ) );
    const QString afterPath = after.writeToDisk( dir.filePath( "after.tif" ) );
    REQUIRE( !beforePath.isEmpty() );
    REQUIRE( !afterPath.isEmpty() );

    Json::Value params( Json::objectValue );
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();

    const double rssBefore = peakRssMiB();
    std::vector<double> mpx;
    for ( int run = 0; run < 5; ++run )
    {
        params["output"] = dir.filePath( QStringLiteral( "chg%1.tif" ).arg( run ) )
                               .toStdString();
        const auto t0 = std::chrono::steady_clock::now();
        const Json::Value result = runOp( "rs:post_classification_change", params );
        const auto t1 = std::chrono::steady_clock::now();
        // Every pixel changed class (cls -> cls+1 mod 4).
        REQUIRE( result["changedPixels"].asUInt64()
                 == static_cast<unsigned long long>( kSide ) * kSide );
        mpx.push_back( static_cast<double>( kSide ) * kSide / 1.0e6
                       / std::chrono::duration<double>( t1 - t0 ).count() );
    }
    report( "post-classification change", mpx, rssBefore, peakRssMiB() );
    CHECK( medianOf( mpx ) > 0.0 );
}
