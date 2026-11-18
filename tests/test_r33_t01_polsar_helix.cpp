// tests/test_r33_t01_polsar_helix.cpp — Yamaguchi helicity handedness
// regression (R33-T01, issue #2127): the helix covariance contributes
// |fh|/4 to every diagonal in BOTH handednesses — only Im C12 = Im C23
// carries the sign. A signed fh/4 decontamination inflates volume and
// residual powers for left-handed targets (SPAN 2.2 vs trace 0.6).
//
// Oracles:
//  * PSD rank-1 helix targets u = (√.2, ±j√.2, −√.2) built through
//    accumulatePolSample/finalizeEnsemble (never hand-written negative
//    diagonals): identical non-negative components, trace-3 = 0.6,
//    decomposition total = trace, helix = 0.6, everything else 0.
//  * Left-handed mixed target (surface + volume + helix) recovers the
//    generating powers exactly.
//  * Clamp boundary: a helix estimate stronger than the cross-pol power
//    clamps volume to 0 (documented SPAN break), outputs stay finite and
//    non-negative.
//  * Registry E2E: a real CFloat32 left-helix scene through
//    rs:sar_polsar_decompose (yamaguchi), reopened and checked band by band.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>

#include <cmath>
#include <complex>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/sar/sar_complex.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_polsar.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_r33_t01_polsar_helix";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

using cd = std::complex<double>;
using cfloat = std::complex<float>;

/// Covariance of a SINGLE deterministic scattering vector (rank-1, PSD by
/// construction) via the production accumulation path.
sicnu::sar::PolEnsemble3 outerCovariance( cd shh, cd shv, cd svv )
{
    sicnu::sar::PolEnsemble3 ens;
    sicnu::sar::accumulatePolSample( ens, shh, shv, svv );
    sicnu::sar::PolEnsemble3 cov;
    REQUIRE( sicnu::sar::finalizeEnsemble( ens, &cov ) );
    return cov;
}

bool writeComplexScene( const QString &path, int width, int height, int bands,
                        const std::vector<std::vector<cfloat>> &bandValues )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height,
                                  bands, GDT_CFloat32, nullptr );
    if ( !ds )
        return false;
    bool ok = true;
    for ( int b = 1; b <= bands && ok; ++b )
        ok = GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, width, height,
                           const_cast<cfloat *>( bandValues[static_cast<size_t>( b - 1 )].data() ),
                           width, height, GDT_CFloat32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

bool declareChannels( const QString &path, const QString &decl )
{
    ensureGdalInit();
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    if ( !ds )
        return false;
    GDALSetMetadataItem( ds, sicnu::sar::kComplexChannelsKey,
                         decl.toUtf8().constData(), nullptr );
    GDALClose( ds );
    return true;
}

std::vector<float> readFloatBand( const QString &path, int band )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> values( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, values.data(), ds.width(), ds.height() ) )
        return {};
    return values;
}

Json::Value runOperator( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

} // anonymous namespace

TEST_CASE( "yamaguchi4 helicity PSD targets conserve trace-3 power", "[r33t01][sar][polsar]" )
{
    const double a = std::sqrt( 0.2 );
    const cd right[3] = { { a, 0.0 }, { 0.0, a }, { -a, 0.0 } };  // u_right
    const cd left[3] = { { a, 0.0 }, { 0.0, -a }, { -a, 0.0 } };   // conj(u_right)

    for ( const auto *u : { right, left } )
    {
        const sicnu::sar::PolEnsemble3 cov =
            outerCovariance( u[0], u[1], u[2] );

        // Same non-negative covariance components in both handednesses;
        // the sign lives ONLY in Im C12 = Im C23.
        REQUIRE( cov.c11 == Approx( 0.2 ).margin( 1e-12 ) );
        REQUIRE( cov.c22 == Approx( 0.2 ).margin( 1e-12 ) );
        REQUIRE( cov.c33 == Approx( 0.2 ).margin( 1e-12 ) );
        REQUIRE( cov.c13.real() == Approx( -0.2 ).margin( 1e-12 ) );
        const double signedQuarter = ( u == right ) ? -0.2 : 0.2;
        REQUIRE( cov.c12.imag() == Approx( signedQuarter ).margin( 1e-12 ) );
        REQUIRE( cov.c23.imag() == Approx( signedQuarter ).margin( 1e-12 ) );

        sicnu::sar::YamaguchiResult r;
        REQUIRE( sicnu::sar::yamaguchi4( cov, &r ) );
        REQUIRE( r.helix == Approx( 0.6 ).margin( 1e-12 ) );
        REQUIRE( r.surface == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( r.doubleBounce == Approx( 0.0 ).margin( 1e-12 ) );
        // The regression: signed fh/4 subtraction reported Pv = 1.6 here.
        REQUIRE( r.volume == Approx( 0.0 ).margin( 1e-12 ) );

        const double trace3 = cov.c11 + cov.c22 + cov.c33;
        REQUIRE( trace3 == Approx( 0.6 ).margin( 1e-12 ) );
        REQUIRE( r.surface + r.doubleBounce + r.volume + r.helix
                 == Approx( trace3 ).margin( 1e-12 ) );
    }
}

TEST_CASE( "yamaguchi4 left-handed mixed target recovers powers exactly",
           "[r33t01][sar][polsar]" )
{
    // surface (fs, β) + volume (fv) + LEFT helix (fh = −0.8).
    const double fs = 2.0;
    const cd beta{ 0.3, 0.1 };
    const double fv = 1.0;
    const double fh = -0.8;
    const double q = std::abs( fh ) / 4.0;

    sicnu::sar::PolEnsemble3 cov;
    cov.c11 = fs + fv + q;
    cov.c22 = 2.0 * fv / 3.0 + q;
    cov.c33 = fs * std::norm( beta ) + fv + q;
    cov.c13 = fs * std::conj( beta ) + fv / 3.0 - q;
    cov.c12 = { 0.0, -fh / 4.0 };
    cov.c23 = { 0.0, -fh / 4.0 };
    cov.samples = 1;

    sicnu::sar::YamaguchiResult r;
    REQUIRE( sicnu::sar::yamaguchi4( cov, &r ) );
    REQUIRE( r.surface == Approx( fs * ( 1.0 + std::norm( beta ) ) ).margin( 1e-12 ) );
    REQUIRE( r.doubleBounce == Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( r.volume == Approx( ( 8.0 / 3.0 ) * fv ).margin( 1e-12 ) );
    REQUIRE( r.helix == Approx( std::abs( fh ) * 0.75 ).margin( 1e-12 ) );

    const double span = cov.c11 + cov.c22 + cov.c33;
    REQUIRE( r.surface + r.doubleBounce + r.volume + r.helix
             == Approx( span ).margin( 1e-12 ) );
}

TEST_CASE( "yamaguchi4 clamps non-physical helix overestimate, stays finite",
           "[r33t01][sar][polsar]" )
{
    // Im C12 = Im C23 = 0.3 estimates |fh| = 1.2, but the cross-pol power
    // is only 0.1 — the decontaminated fv goes negative and must clamp to
    // 0 (documented SPAN break), never leak a negative/infinite power.
    sicnu::sar::PolEnsemble3 cov;
    cov.c11 = 0.5;
    cov.c22 = 0.1;
    cov.c33 = 0.5;
    cov.c13 = 0.1;
    cov.c12 = { 0.0, 0.3 };
    cov.c23 = { 0.0, 0.3 };
    cov.samples = 1;

    sicnu::sar::YamaguchiResult r;
    REQUIRE( sicnu::sar::yamaguchi4( cov, &r ) );
    REQUIRE( r.ok );
    REQUIRE( std::isfinite( r.surface ) );
    REQUIRE( std::isfinite( r.doubleBounce ) );
    REQUIRE( std::isfinite( r.volume ) );
    REQUIRE( std::isfinite( r.helix ) );
    REQUIRE( r.surface >= 0.0 );
    REQUIRE( r.doubleBounce >= 0.0 );
    REQUIRE( r.volume >= 0.0 );
    REQUIRE( r.volume == Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( r.helix == Approx( 1.2 * 0.75 ).margin( 1e-12 ) );
}

TEST_CASE( "rs:sar_polsar_decompose yamaguchi on a left-helix scene",
           "[r33t01][sar][polsar][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Uniform LEFT-handed circular target: u = (√.2, −j√.2, −√.2).
    // windowSize = 1 → per-pixel covariance is exactly u·u^H.
    constexpr int kWidth = 12;
    constexpr int kHeight = 12;
    const float a = std::sqrt( 0.2f );
    const cfloat shh( a, 0.0f );
    const cfloat shv( 0.0f, -a );
    const cfloat svv( -a, 0.0f );

    const std::vector<cfloat> hh( static_cast<size_t>( kWidth ) * kHeight, shh );
    const std::vector<cfloat> hv( static_cast<size_t>( kWidth ) * kHeight, shv );
    const std::vector<cfloat> vv( static_cast<size_t>( kWidth ) * kHeight, svv );

    const QString input = tmp.filePath( "left_helix.tif" );
    REQUIRE( writeComplexScene( input, kWidth, kHeight, 3, { hh, hv, vv } ) );
    REQUIRE( declareChannels( input, QStringLiteral( "HH;HV;VV" ) ) );

    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = tmp.filePath( "yamaguchi.tif" ).toStdString();
    params["decomposition"] = "yamaguchi";
    params["windowSize"] = 1;
    Json::Value result;
    REQUIRE_NOTHROW( result = runOperator( "rs:sar_polsar_decompose", params ) );
    REQUIRE( result["bands"].asString() == "surface;double_bounce;volume;helix;span" );

    const auto surface = readFloatBand( tmp.filePath( "yamaguchi.tif" ), 1 );
    const auto dbl = readFloatBand( tmp.filePath( "yamaguchi.tif" ), 2 );
    const auto vol = readFloatBand( tmp.filePath( "yamaguchi.tif" ), 3 );
    const auto helix = readFloatBand( tmp.filePath( "yamaguchi.tif" ), 4 );
    const auto span = readFloatBand( tmp.filePath( "yamaguchi.tif" ), 5 );
    REQUIRE( surface.size() == static_cast<size_t>( kWidth ) * kHeight );

    for ( size_t i = 0; i < surface.size(); ++i )
    {
        INFO( "pixel " << i );
        REQUIRE( surface[i] == Approx( 0.0f ).margin( 1e-5 ) );
        REQUIRE( dbl[i] == Approx( 0.0f ).margin( 1e-5 ) );
        // Pre-fix this band reported 1.6 (volume inflated by +|fh|/4).
        REQUIRE( vol[i] == Approx( 0.0f ).margin( 1e-5 ) );
        REQUIRE( helix[i] == Approx( 0.6f ).margin( 1e-5 ) );
        // Pre-fix this band reported 2.2 (SPAN inflation, trace-3 is 0.6).
        REQUIRE( span[i] == Approx( 0.6f ).margin( 1e-5 ) );
    }
}
