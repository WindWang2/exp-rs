// tests/test_r33_t01_network_velocity.cpp — velocity regression anchor
// regression (R33-T01, issue #2128): the linear velocity fit must include
// the reference epoch's KNOWN displacement (u0 = 0 at t0) as a data point.
// Solved-epochs-only regression (a) collapses to a zero denominator for a
// two-epoch pixel (NaN velocity) and (b) biases the slope whenever the
// reference epoch carries information: truth u = [0, 1, 0, 1] mm on
// t = (0, 1, 2, 3) years regressed over epochs {1,2,3} alone gives
// 0 mm/yr; with the anchor it is 0.2 mm/yr (hand-derived OLS).
//
// The epoch-displacement solve itself is untouched: a consistent network
// still recovers the exact u table, RMS stays 0, and solvedEpochs keeps
// its meaning (count of NON-reference solved epochs).
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
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/sar/sar_network_inversion.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using namespace sicnu::sar;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_r33_t01_network_velocity";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

constexpr double kMm = 1e-3;

bool writeFloatRaster( const QString &path, const std::vector<float> &values, int width,
                       int height )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, width, height,
                                  const_cast<float *>( values.data() ), width, height,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

std::vector<float> readBand( const QString &path, int band = 1 )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> values( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, values.data(), ds.width(), ds.height() ) )
        return {};
    return values;
}

} // anonymous namespace

TEST_CASE( "network inversion velocity anchors on the reference epoch (#2128)",
           "[r33t01][sar][network-inversion]" )
{
    // 4 epochs, 5 pairs: (1,0) (2,0) (3,0) (2,1) (3,2) — connected,
    // overdetermined; truth u = [0, 1, 0, 1] mm is NON-linear in time.
    NetworkInversionProblem problem;
    problem.epochCount = 4;
    problem.pairCount = 5;
    problem.pairMasterEpoch = { 1, 2, 3, 2, 3 };
    problem.pairSlaveEpoch = { 0, 0, 0, 1, 2 };
    problem.pairTemporalYears = { 1.0, 2.0, 3.0, 1.0, 1.0 };
    REQUIRE( problem.isValid() );
    NetworkInversionSolver solver( problem );

    const double temporal[] = { 0.0, 1.0, 2.0, 3.0 };
    // d_p = u_master − u_slave for the truth table.
    const double d[] = { 1.0 * kMm, 0.0 * kMm, 1.0 * kMm, -1.0 * kMm, 1.0 * kMm };

    std::vector<double> u( 4, 0.0 );
    double velocity = 0.0;
    double rms = 0.0;
    int solvedEpochs = -1;
    int droppedPairs = -1;
    QString error;
    REQUIRE( solver.solvePixel( d, temporal, u.data(), &velocity, &rms,
                                &solvedEpochs, &droppedPairs, &error ) );

    // The solve itself is unchanged: exact recovery, zero residual.
    REQUIRE( u[0] == Approx( 0.0 ).margin( 1e-15 ) );
    REQUIRE( u[1] == Approx( 1.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( u[2] == Approx( 0.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( u[3] == Approx( 1.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( rms < 1e-15 );
    REQUIRE( solvedEpochs == 3 );
    REQUIRE( droppedPairs == 0 );

    // Hand OLS over the anchored points (0,0) (1,1) (2,0) (3,1) [mm, yr]:
    // slope = Σ(t−1.5)(u−0.5) / Σ(t−1.5)² = 1.0/5.0 mm/yr = 0.2 mm/yr.
    // Solved-only regression over {1,2,3} gave exactly 0 (the #2128 bias).
    REQUIRE( velocity == Approx( 0.2 * kMm ).margin( 1e-12 ) );

    // Time-origin invariance: shifting the whole temporal base leaves the
    // slope unchanged (standard OLS property; pins that the anchor uses
    // t0, not the index 0).
    const double shifted[] = { 7.3, 8.3, 9.3, 10.3 };
    double velocityShifted = 0.0;
    REQUIRE( solver.solvePixel( d, shifted, u.data(), &velocityShifted, nullptr, nullptr,
                                nullptr, &error ) );
    REQUIRE( velocityShifted == Approx( 0.2 * kMm ).margin( 1e-12 ) );

    // Index-as-time fallback (epochTemporalYears = nullptr): anchor t0=0.
    double velocityIndexed = 0.0;
    REQUIRE( solver.solvePixel( d, nullptr, u.data(), &velocityIndexed, nullptr, nullptr,
                                nullptr, &error ) );
    REQUIRE( velocityIndexed == Approx( 0.2 * kMm ).margin( 1e-12 ) );
}

TEST_CASE( "network inversion two-epoch pixel yields a finite velocity (#2128)",
           "[r33t01][sar][network-inversion]" )
{
    // 3 epochs but pairs 2..3 missing everywhere: the pixel solves only
    // epoch 1 against the reference — the anchored fit is the two-point
    // slope u1/t1. Solved-only (single point) produced NaN.
    NetworkInversionProblem problem;
    problem.epochCount = 3;
    problem.pairCount = 3;
    problem.pairMasterEpoch = { 1, 2, 2 };
    problem.pairSlaveEpoch = { 0, 0, 1 };
    problem.pairTemporalYears = { 1.0, 2.0, 1.0 };
    NetworkInversionSolver solver( problem );

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double temporal[] = { 0.0, 2.5, 5.0 };
    const double d[] = { 5.0 * kMm, nan, nan };

    std::vector<double> u( 3, 0.0 );
    double velocity = 0.0;
    int solvedEpochs = -1;
    int droppedPairs = -1;
    QString error;
    REQUIRE( solver.solvePixel( d, temporal, u.data(), &velocity, nullptr,
                                &solvedEpochs, &droppedPairs, &error ) );
    REQUIRE( std::isnan( u[2] ) );
    REQUIRE( u[1] == Approx( 5.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( solvedEpochs == 1 );
    REQUIRE( droppedPairs == 2 );
    // 5 mm over 2.5 years anchored at (0, 0).
    REQUIRE( velocity == Approx( 2.0 * kMm ).margin( 1e-12 ) );
}

TEST_CASE( "rs:sar_network_inversion velocity raster carries the anchored fit",
           "[r33t01][sar][network-inversion][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kN = 5;
    // Same non-linear truth as the kernel case: u = [0, 1, 0, 1] mm on
    // t = (0, 1, 2, 3) yr; pairs (1,0) (2,0) (3,0) (2,1) (3,2).
    const double u[] = { 0.0, 1e-3, 0.0, 1e-3 };
    const int pairMaster[] = { 1, 2, 3, 2, 3 };
    const int pairSlave[] = { 0, 0, 0, 1, 2 };

    Json::Value inputs( Json::arrayValue );
    for ( int p = 0; p < 5; ++p )
    {
        std::vector<float> plane( static_cast<size_t>( kN ) * kN,
                                  static_cast<float>( u[pairMaster[p]] - u[pairSlave[p]] ) );
        const QString path = tmp.filePath( QString( "d_%1.tif" ).arg( p ) );
        REQUIRE( writeFloatRaster( path, plane, kN, kN ) );
        inputs.append( path.toStdString() );
    }
    Json::Value pairsJson( Json::arrayValue );
    for ( int p = 0; p < 5; ++p )
    {
        Json::Value pair( Json::arrayValue );
        pair.append( pairMaster[p] );
        pair.append( pairSlave[p] );
        pairsJson.append( pair );
    }
    Json::Value temporal( Json::arrayValue );
    for ( const double t : { 0.0, 1.0, 2.0, 3.0 } )
        temporal.append( t );

    const QString velocityPath = tmp.filePath( "velocity.tif" );
    Json::Value params( Json::objectValue );
    params["displacementInputs"] = inputs;
    params["pairs"] = pairsJson;
    params["epochTemporalYears"] = temporal;
    params["velocityOutput"] = velocityPath.toStdString();

    auto op = RSOperatorRegistry::instance().create( "rs:sar_network_inversion" );
    REQUIRE( op != nullptr );
    RSOperatorContext ctx;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, ctx ) );
    REQUIRE( result["solvedPixels"].asInt64() == kN * kN );

    const std::vector<float> velocity = readBand( velocityPath );
    REQUIRE( velocity.size() == static_cast<size_t>( kN ) * kN );
    for ( const float v : velocity )
        REQUIRE( v == Approx( 0.2e-3f ).margin( 1e-9f ) );
}
