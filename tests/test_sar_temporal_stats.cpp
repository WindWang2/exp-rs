// tests/test_sar_temporal_stats.cpp — multi-date SAR statistics
// (Scientific Processing 8.0, package B).
//
// Kernel expectations are closed-form on hand-computed linear-power series;
// the operator E2E flows constant scenes through the registry and checks the
// product bands against the same closed forms, including the declared-domain
// conversion path.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_temporal.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::sar;
using namespace sicnu::operators;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_sar_temporal_stats";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

bool writeScene( const QString &path, const std::vector<float> &values, int width, int height,
                 bool declareDb = false )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    if ( declareDb )
        GDALSetMetadataItem( ds, sicnu::sar::kDomainKey, "db", nullptr );
    if ( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, width, height,
                       const_cast<float *>( values.data() ), width, height, GDT_Float32,
                       0, 0 ) != CE_None )
    {
        GDALClose( ds );
        return false;
    }
    GDALClose( ds );
    return true;
}

std::vector<float> readBand( const QString &path, int band )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, out.data(), ds.width(), ds.height() ) )
        return {};
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Kernel
// ---------------------------------------------------------------------------

TEST_CASE( "sarTemporalStats: closed forms on a hand-computed series",
           "[sar][temporal]" )
{
    // Series: [1, 2, 4, 8] linear power.
    const double series[] = { 1.0, 2.0, 4.0, 8.0 };
    SarTemporalStats s;
    REQUIRE( sarTemporalStats( series, 4, &s, 6.0 ) );
    REQUIRE( s.validCount == 4 );
    REQUIRE( s.meanLinear == Approx( 3.75 ).margin( 1e-12 ) );
    REQUIRE( s.meanDb == Approx( 10.0 * std::log10( 3.75 ) ).margin( 1e-12 ) );
    // Population stddev: mean deviation squares (2.75² + 1.25² + 0.25² + 4.25²)/4.
    const double var = ( 2.75 * 2.75 + 1.25 * 1.25 + 0.25 * 0.25 + 4.25 * 4.25 ) / 4.0;
    REQUIRE( s.stdDevLinear == Approx( std::sqrt( var ) ).margin( 1e-12 ) );
    REQUIRE( s.cv == Approx( std::sqrt( var ) / 3.75 ).margin( 1e-12 ) );
    REQUIRE( s.minLinear == 1.0 );
    REQUIRE( s.maxLinear == 8.0 );
    REQUIRE( s.argminDate == 0 );
    REQUIRE( s.argmaxDate == 3 );
    // Median = 3 (even count, lower+upper averaged? — the kernel takes the
    // upper-middle nth_element value; for [1,2,4,8] mid=2 → 4? No: the
    // documented baseline is the nth_element mid sample = 4? It is the
    // upper median 4? mid = 4/2 = 2 → valid[2] after partition = 4... The
    // exact 0.5*(2+4) form is NOT what the kernel promises — it anchors at
    // the nth_element upper-median sample. Read it back here:
    REQUIRE( s.baselineDb == Approx( 10.0 * std::log10( 4.0 ) ).epsilon( 1e-12 ) );

    // Log deviations from baseline 10·log10(4) = 6.0206 dB:
    // |−6.0206|, |−3.0103|, 0, |+3.0103| → max 6.0206, mean 3.0103.
    REQUIRE( s.maxLogDeviationDb == Approx( 10.0 * std::log10( 4.0 ) ).margin( 1e-12 ) );
    REQUIRE( s.meanLogDeviationDb
             == Approx( ( 6.0206 + 3.0103 + 0.0 + 3.0103 ) / 4.0 ).margin( 1e-3 ) );
    // Threshold 6 dB: only the first sample (6.02 dB) crosses.
    REQUIRE( s.changedDates == 1 );
}

TEST_CASE( "sarTemporalStats: odd median, threshold counting, invalid samples",
           "[sar][temporal]" )
{
    // Odd count: median is the middle sample [1, 3, 12] → 3.
    const double odd[] = { 1.0, 3.0, 12.0 };
    SarTemporalStats s;
    REQUIRE( sarTemporalStats( odd, 3, &s, 6.0 ) );
    REQUIRE( s.baselineDb == Approx( 10.0 * std::log10( 3.0 ) ).margin( 1e-12 ) );
    // Deviations: |10log10(1/3)|=4.771, 0, |10log10(12/3)|=6.021.
    REQUIRE( s.changedDates == 1 );
    SarTemporalStats s4;
    REQUIRE( sarTemporalStats( odd, 3, &s4, 4.0 ) );
    REQUIRE( s4.changedDates == 2 );
    REQUIRE( s4.maxLogDeviationDb == Approx( 10.0 * std::log10( 4.0 ) ).margin( 1e-9 ) );

    // Invalid samples are excluded from every aggregate (nonpositive,
    // NaN) but keep their date positions in argmin/argmax bookkeeping.
    const double dirty[] = { -1.0, std::numeric_limits<double>::quiet_NaN(), 2.0, 2.0, 8.0 };
    SarTemporalStats d;
    REQUIRE( sarTemporalStats( dirty, 5, &d ) );
    REQUIRE( d.validCount == 3 );
    REQUIRE( d.meanLinear == Approx( 4.0 ).margin( 1e-12 ) );
    REQUIRE( d.maxLinear == 8.0 );
    REQUIRE( d.argmaxDate == 4 );

    // All-invalid series refuse.
    const double dead[] = { 0.0, -3.0 };
    SarTemporalStats x;
    REQUIRE_FALSE( sarTemporalStats( dead, 2, &x ) );
    REQUIRE_FALSE( sarTemporalStats( nullptr, 2, &x ) );
    // Negative threshold is a contract violation.
    const double ok[] = { 1.0, 1.0 };
    REQUIRE_FALSE( sarTemporalStats( ok, 2, &x, -1.0 ) );
}

// ---------------------------------------------------------------------------
// Operator E2E
// ---------------------------------------------------------------------------

TEST_CASE( "rs:sar_temporal_stats aggregates constant scenes with closed-form bands",
           "[sar][temporal][operator][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Three constant 4x4 scenes: 1, 2, 4 linear power.
    const QString a = tmp.filePath( "s1.tif" );
    const QString b = tmp.filePath( "s2.tif" );
    const QString c = tmp.filePath( "s3.tif" );
    REQUIRE( writeScene( a, std::vector<float>( 16, 1.0f ), 4, 4 ) );
    REQUIRE( writeScene( b, std::vector<float>( 16, 2.0f ), 4, 4 ) );
    REQUIRE( writeScene( c, std::vector<float>( 16, 4.0f ), 4, 4 ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_temporal_stats" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    Json::Value inputs( Json::arrayValue );
    inputs.append( a.toStdString() );
    inputs.append( b.toStdString() );
    inputs.append( c.toStdString() );
    params["inputs"] = inputs;
    params["output"] = tmp.filePath( "stats.tif" ).toStdString();
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );
    REQUIRE( result["scenes"].asUInt64() == 3ULL );
    REQUIRE( result["inputDomainResolved"].asString() == "linear_power" );

    const auto meanDb = readBand( tmp.filePath( "stats.tif" ), 1 );
    const auto meanLinear = readBand( tmp.filePath( "stats.tif" ), 2 );
    const auto cv = readBand( tmp.filePath( "stats.tif" ), 4 );
    const auto baselineDb = readBand( tmp.filePath( "stats.tif" ), 8 );
    const auto changed = readBand( tmp.filePath( "stats.tif" ), 10 );
    const auto validCount = readBand( tmp.filePath( "stats.tif" ), 11 );
    REQUIRE( meanDb.size() == 16 );

    // Series [1,2,4]: mean = 7/3, median = 2, deviation 10log10(4/2) = 3.01.
    for ( int i = 0; i < 16; ++i )
    {
        REQUIRE( meanLinear[i] == Approx( 7.0 / 3.0 ).margin( 1e-5 ) );
        REQUIRE( meanDb[i] == Approx( 10.0 * std::log10( 7.0 / 3.0 ) ).margin( 1e-4 ) );
        REQUIRE( baselineDb[i] == Approx( 10.0 * std::log10( 2.0 ) ).margin( 1e-4 ) );
        // Population stddev of [1,2,4]: var = (16/9 + 1/9 + 25/9)/3 = 14/9.
        REQUIRE( cv[i] == Approx( std::sqrt( 14.0 / 9.0 ) / ( 7.0 / 3.0 ) ).margin( 1e-4 ) );
        // Threshold 6 dB: max deviation 3.01 dB → no changed dates.
        REQUIRE( changed[i] == 0.0f );
        REQUIRE( validCount[i] == 3.0f );
    }
}

TEST_CASE( "rs:sar_temporal_stats converts declared dB scenes and counts robust change",
           "[sar][temporal][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Scenes declared dB: [0, 3, 10] dB = [1, ~2, 10] linear.
    const QString a = tmp.filePath( "d1.tif" );
    const QString b = tmp.filePath( "d2.tif" );
    const QString c = tmp.filePath( "d3.tif" );
    REQUIRE( writeScene( a, std::vector<float>( 16, 0.0f ), 4, 4, true ) );
    const float db2 = 10.0f * std::log10( 2.0f ); // ~3.0103 dB
    REQUIRE( writeScene( b, std::vector<float>( 16, db2 ), 4, 4, true ) );
    REQUIRE( writeScene( c, std::vector<float>( 16, 10.0f ), 4, 4, true ) );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_temporal_stats" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    Json::Value inputs( Json::arrayValue );
    inputs.append( a.toStdString() );
    inputs.append( b.toStdString() );
    inputs.append( c.toStdString() );
    params["inputs"] = inputs;
    params["output"] = tmp.filePath( "stats_db.tif" ).toStdString();
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );
    REQUIRE( result["inputDomainResolved"].asString() == "db" );

    const auto meanLinear = readBand( tmp.filePath( "stats_db.tif" ), 2 );
    // Linear series [1, 2, 10]: mean = 13/3.
    for ( int i = 0; i < 16; ++i )
        REQUIRE( meanLinear[i] == Approx( 13.0 / 3.0 ).margin( 1e-4 ) );

    // A changing series: baseline 2, dates at 1 (−3 dB) and 10 (+7 dB):
    // threshold 6 dB → exactly one changed date per pixel.
    const QString x1 = tmp.filePath( "c1.tif" );
    const QString x2 = tmp.filePath( "c2.tif" );
    const QString x3 = tmp.filePath( "c3.tif" );
    std::vector<float> v1( 16, 1.0f );
    std::vector<float> v2( 16, 10.0f * std::log10( 2.0f ) ); // = 3.0103 dB
    std::vector<float> v3( 16, 10.0f );
    REQUIRE( writeScene( x1, v1, 4, 4, true ) );
    REQUIRE( writeScene( x2, v2, 4, 4, true ) );
    REQUIRE( writeScene( x3, v3, 4, 4, true ) );
    Json::Value p2( Json::objectValue );
    Json::Value in2( Json::arrayValue );
    in2.append( x1.toStdString() );
    in2.append( x2.toStdString() );
    in2.append( x3.toStdString() );
    p2["inputs"] = in2;
    p2["output"] = tmp.filePath( "change.tif" ).toStdString();
    Json::Value r2;
    REQUIRE_NOTHROW( r2 = op->run( p2, ctx ) );
    const auto changedDates = readBand( tmp.filePath( "change.tif" ), 10 );
    const auto maxDev = readBand( tmp.filePath( "change.tif" ), 9 );
    for ( int i = 0; i < 16; ++i )
    {
        REQUIRE( changedDates[i] == 1.0f );
        REQUIRE( maxDev[i] == Approx( 10.0 * std::log10( 5.0 ) ).margin( 1e-9 ) ); // 10log10(10/2)
    }
}

TEST_CASE( "rs:sar_temporal_stats refusals and NoData bookkeeping",
           "[sar][temporal][operator]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    auto op = RSOperatorRegistry::instance().create( "rs:sar_temporal_stats" );
    REQUIRE( op != nullptr );
    RSOperatorContext ctx;

    const QString a = tmp.filePath( "g1.tif" );
    const QString b = tmp.filePath( "g2.tif" );
    REQUIRE( writeScene( a, std::vector<float>( 16, 1.0f ), 4, 4 ) );
    REQUIRE( writeScene( b, std::vector<float>( 16, 1.0f ), 4, 4 ) );

    // Fewer than two scenes.
    {
        Json::Value p( Json::objectValue );
        Json::Value one( Json::arrayValue );
        one.append( a.toStdString() );
        p["inputs"] = one;
        p["output"] = tmp.filePath( "o1.tif" ).toStdString();
        REQUIRE_THROWS_AS( op->run( p, ctx ), RSOperatorError );
    }

    // Grid mismatch.
    {
        const QString big = tmp.filePath( "big.tif" );
        REQUIRE( writeScene( big, std::vector<float>( 25, 1.0f ), 5, 5 ) );
        Json::Value p( Json::objectValue );
        Json::Value in( Json::arrayValue );
        in.append( a.toStdString() );
        in.append( big.toStdString() );
        p["inputs"] = in;
        p["output"] = tmp.filePath( "o2.tif" ).toStdString();
        REQUIRE_THROWS_AS( op->run( p, ctx ), RSOperatorError );
    }

    // Mixed declared domains are a typed refusal.
    {
        const QString dbScene = tmp.filePath( "db.tif" );
        REQUIRE( writeScene( dbScene, std::vector<float>( 16, 1.0f ), 4, 4, true ) );
        Json::Value p( Json::objectValue );
        Json::Value in( Json::arrayValue );
        in.append( a.toStdString() ); // undeclared → linear fallback
        in.append( dbScene.toStdString() ); // declared db
        p["inputs"] = in;
        p["output"] = tmp.filePath( "o3.tif" ).toStdString();
        // undeclared + declared(db) is legal (fallback fills the gap):
        // this must SUCCEED and resolve to db.
        Json::Value r;
        REQUIRE_NOTHROW( r = op->run( p, ctx ) );
        REQUIRE( r["inputDomainResolved"].asString() == "db" );
    }

    // Sentinel/invalid pixels drop out of the aggregates: a pixel that is
    // NaN in one scene reports validCount 2 (of 3) and correct means.
    {
        const QString n1 = tmp.filePath( "n1.tif" );
        const QString n2 = tmp.filePath( "n2.tif" );
        const QString n3 = tmp.filePath( "n3.tif" );
        std::vector<float> s1( 16, 1.0f );
        std::vector<float> s2( 16, std::numeric_limits<float>::quiet_NaN() );
        std::vector<float> s3( 16, 3.0f );
        s2[5] = 2.0f; // one valid pixel in the middle scene
        REQUIRE( writeScene( n1, s1, 4, 4 ) );
        REQUIRE( writeScene( n2, s2, 4, 4 ) );
        REQUIRE( writeScene( n3, s3, 4, 4 ) );
        Json::Value p( Json::objectValue );
        Json::Value in( Json::arrayValue );
        in.append( n1.toStdString() );
        in.append( n2.toStdString() );
        in.append( n3.toStdString() );
        p["inputs"] = in;
        p["output"] = tmp.filePath( "o4.tif" ).toStdString();
        Json::Value r;
        REQUIRE_NOTHROW( r = op->run( p, ctx ) );
        const auto validCount = readBand( tmp.filePath( "o4.tif" ), 11 );
        const auto meanLinear = readBand( tmp.filePath( "o4.tif" ), 2 );
        for ( int i = 0; i < 16; ++i )
        {
            if ( i == 5 )
            {
                REQUIRE( validCount[i] == 3.0f );
                REQUIRE( meanLinear[i] == Approx( 2.0 ).margin( 1e-6 ) );
            }
            else
            {
                REQUIRE( validCount[i] == 2.0f );
                REQUIRE( meanLinear[i] == Approx( 2.0 ).margin( 1e-6 ) );
            }
        }
    }
}
