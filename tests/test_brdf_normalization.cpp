// test_brdf_normalization.cpp — radiometric-physics-11 work package E.
//
// Independent-oracle policy:
//  * Published special values: k_vol = 0 and k_geo = −1 exactly at nadir sun
//    + nadir view (Lucht et al. 2000 kernel definitions); both kernels are
//    reciprocal (symmetric under sun↔view swap); k_vol is at its hotspot
//    (ξ = 0) maximum for f_vol > 0.
//  * Hand-computed reference values: worked numerically from the published
//    formulas in the comments with degrees→radians done by hand (30° =
//    0.5236 rad, sin/cos from standard tables), e.g.
//    k_vol(30°,0°,Δφ) : ξ = 30°, ((π/2−0.5236)·0.8660+0.5)/1.8660 − 0.7854
//    = 0.7539 − 0.7854 = −0.0315.
//  * An exact linear-algebra anchor for the c-factor: pairs generated as
//    y = a + b·x must give back c = a/b and correction = c·x leveling the
//    means (that is the estimator's definition, so it is an identity oracle).
//
// Negative tests: nonphysical zeniths, nonphysical weights (factor ≤ 0),
// too-few pairs, degenerate slopes, null outputs, NaN pass-through.

#include "processing/algorithms/brdf_normalization.h"
#include "processing/algorithms/satellite_products.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace BrdfNormalization;

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
} // namespace

TEST_CASE( "kernel special values at nadir sun + nadir view", "[brdf]" )
{
    CHECK( rossThick( 0.0, 0.0, 0.0 ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    CHECK( liSparseReciprocal( 0.0, 0.0, 0.0 ) == Catch::Approx( -1.0 ).margin( 1e-12 ) );
}

TEST_CASE( "kernels are reciprocal under sun/view swap", "[brdf]" )
{
    for ( const auto &g : { std::make_tuple( 30.0, 45.0, 0.0 ),
                            std::make_tuple( 20.0, 60.0, 90.0 ),
                            std::make_tuple( 50.0, 10.0, 180.0 ) } )
    {
        const double kVol1 = rossThick( std::get<0>( g ), std::get<1>( g ), std::get<2>( g ) );
        const double kVol2 = rossThick( std::get<1>( g ), std::get<0>( g ), std::get<2>( g ) );
        CHECK( kVol1 == Catch::Approx( kVol2 ).margin( 1e-12 ) );

        const double kGeo1 =
            liSparseReciprocal( std::get<0>( g ), std::get<1>( g ), std::get<2>( g ) );
        const double kGeo2 =
            liSparseReciprocal( std::get<1>( g ), std::get<0>( g ), std::get<2>( g ) );
        CHECK( kGeo1 == Catch::Approx( kGeo2 ).margin( 1e-12 ) );
    }
}

TEST_CASE( "hand-computed kernel reference values", "[brdf]" )
{
    // k_vol(30°, 0°, ·): cosξ = cos30° = 0.86603, ξ = 0.52360 rad
    //   ((π/2 − 0.52360)·0.86603 + sin 0.52360) / (cos30° + 1) − π/4
    //   = (1.04720·0.86603 + 0.50000)/1.86603 − 0.78540 = −0.03151
    CHECK( rossThick( 30.0, 0.0, 0.0 ) == Catch::Approx( -0.03151 ).margin( 1e-4 ) );

    // k_vol(40°, 30°, 0°): cosξ = 0.76604·0.86603 + 0.64279·0.5 = 0.98481,
    //   ξ = 0.17453; numerator (1.57080−0.17453)·0.98481 + 0.17365 = 1.54879;
    //   denominator 0.76604 + 0.86603 = 1.63207 → 0.94918 − 0.78540 = 0.16378
    CHECK( rossThick( 40.0, 30.0, 0.0 ) == Catch::Approx( 0.16378 ).margin( 1e-4 ) );

    // k_geo(30°, 0°, ·): tan30° = 0.57735, sec30° = 1.15470, sec0° = 1;
    //   D = 0.57735, D' = 0.57735; cos t = 2·0.57735/2.15470 = 0.53583,
    //   t = 1.00685; O = (1.00685 − sin t·cos t)·2.15470/π
    //   = (1.00685 − 0.84357)·0.68583 = 0.11194;
    //   k_geo = 0.11194 − 1.15470 − 1 + 0.57735 = −0.46541
    CHECK( liSparseReciprocal( 30.0, 0.0, 0.0 ) == Catch::Approx( -0.46541 ).margin( 1e-4 ) );

    // Hotspot: at ξ = 0 with equal zeniths k_vol is maximal along φ.
    const double hotspot = rossThick( 45.0, 45.0, 0.0 );
    CHECK( hotspot >= rossThick( 45.0, 45.0, 90.0 ) );
    CHECK( hotspot >= rossThick( 45.0, 45.0, 180.0 ) );
}

TEST_CASE( "anisotropy factor refuses nonphysical geometry and weights", "[brdf]" )
{
    double f = 0.0;
    QString err;

    CHECK( anisotropyFactor( 30.0, 0.0, 0.0, 0.3, 0.6, &f, &err ) );
    CHECK( f > 0.0 );

    CHECK_FALSE( anisotropyFactor( -1.0, 0.0, 0.0, 0.3, 0.6, &f, &err ) );
    CHECK_FALSE( anisotropyFactor( 90.0, 0.0, 0.0, 0.3, 0.6, &f, &err ) );
    CHECK_FALSE( anisotropyFactor( 30.0, 90.0, 0.0, 0.3, 0.6, &f, &err ) );
    CHECK_FALSE( anisotropyFactor( 30.0, 0.0, kNaN, 0.3, 0.6, &f, &err ) );
    CHECK_FALSE( anisotropyFactor( 30.0, 0.0, 0.0, kNaN, 0.6, &f, &err ) );

    // Weights negative enough to flip the factor nonphysical must refuse.
    CHECK_FALSE( anisotropyFactor( 60.0, 60.0, 180.0, 100.0, -1000.0, &f, &err ) );
    CHECK_FALSE( err.isEmpty() );
}

TEST_CASE( "kernel-driven normalization identity and ratio semantics", "[brdf]" )
{
    float out = 0.0f;
    QString err;

    // Nadir observation → nadir reference with same sun: identity.
    REQUIRE( normalizeKernelDriven( 0.42f, 25.0, 0.0, 0.0, 0.2, 0.5, &out, &err ) );
    CHECK( out == Catch::Approx( 0.42f ).margin( 1e-6 ) );

    // f(G_ref)=f(G_obs) construction: zero weights → factor 1 everywhere.
    REQUIRE( normalizeKernelDriven( 0.30f, 50.0, 40.0, 120.0, 0.0, 0.0, &out, &err ) );
    CHECK( out == Catch::Approx( 0.30f ).margin( 1e-6 ) );

    // Ratio semantics: out = value·f(ref)/f(obs), independently recomputed.
    const double fObs = 1.0 + 0.3 * rossThick( 40.0, 30.0, 60.0 )
                        + 0.5 * liSparseReciprocal( 40.0, 30.0, 60.0 );
    const double fRef = 1.0 + 0.3 * rossThick( 40.0, 0.0, 0.0 )
                        + 0.5 * liSparseReciprocal( 40.0, 0.0, 0.0 );
    REQUIRE( normalizeKernelDriven( 0.50f, 40.0, 30.0, 60.0, 0.3, 0.5, &out, &err ) );
    CHECK( out == Catch::Approx( 0.50 * fRef / fObs ).epsilon( 1e-5 ) );

    // NaN passes through without refusing.
    REQUIRE( normalizeKernelDriven(
        std::numeric_limits<float>::quiet_NaN(), 40.0, 30.0, 60.0, 0.3, 0.5, &out, &err ) );
    CHECK( std::isnan( out ) );

    // Unusable observation geometry refuses without writing.
    out = 0.0f;
    CHECK_FALSE( normalizeKernelDriven( 0.5f, 40.0, 95.0, 60.0, 0.3, 0.5, &out, &err ) );
    CHECK( out == 0.0f );
    CHECK_FALSE( normalizeKernelDriven( 0.5f, 40.0, 30.0, 60.0, 0.3, 0.5, nullptr, &err ) );
}

TEST_CASE( "c-factor recovers a/b exactly from linear pairs", "[brdf]" )
{
    // Pairs generated as y = a + b·x: the OLS fit must return exactly c = a/b,
    // and the leveling correction must map the date-2 mean onto date-1 mean.
    const double a = 0.02, b = 0.9;
    PairRegression reg;
    double x = 0.05;
    for ( int i = 0; i < 200; ++i )
    {
        reg.add( x, a + b * x );
        x += 0.0035;
    }
    double c = 0.0;
    REQUIRE( reg.count() == 200 );
    REQUIRE( reg.fitCFactor( 30, &c ) );
    CHECK( c == Catch::Approx( a / b ).margin( 1e-9 ) );

    // Identity oracle: mean(c·x) equals mean(y) for the OLS line.
    double sx = 0.0, sy = 0.0;
    double x2 = 0.05;
    for ( int i = 0; i < 200; ++i )
    {
        sx += x2;
        sy += a + b * x2;
        x2 += 0.0035;
    }
    CHECK( c * ( sx / 200.0 ) == Catch::Approx( sy / 200.0 ).margin( 1e-9 ) );

    // Application: rho2' = c·rho2, NaN passes through.
    CHECK( applyPairNormalization( 0.4f, c ) == Catch::Approx( 0.4f * a / b ).epsilon( 1e-6 ) );
    CHECK( std::isnan(
        applyPairNormalization( std::numeric_limits<float>::quiet_NaN(), c ) ) );
}

TEST_CASE( "c-factor refuses weak samples and degenerate fits", "[brdf]" )
{
    double c = 0.0;
    QString err;

    PairRegression tooFew;
    tooFew.add( 0.1, 0.12 );
    tooFew.add( 0.2, 0.21 );
    CHECK_FALSE( tooFew.fitCFactor( 30, &c, &err ) );
    CHECK( err.contains( QLatin1String( "pairs" ) ) );
    CHECK_FALSE( tooFew.fitCFactor( 1, &c, &err ) ); // absolute floor of 2 pairs

    // Vertical-line sample (all x equal): zero slope → degenerate refusal.
    PairRegression vertical;
    for ( int i = 0; i < 50; ++i )
        vertical.add( 0.3, 0.3 + 0.01 * i );
    CHECK_FALSE( vertical.fitCFactor( 30, &c, &err ) );
    CHECK( err.contains( QLatin1String( "degenerate" ) ) );

    // Non-positive samples are never usable pairs.
    PairRegression withJunk;
    for ( int i = 0; i < 40; ++i )
        withJunk.add( 0.1 + 0.01 * i, 0.11 + 0.01 * i );
    withJunk.add( -0.5, 0.3 );          // ignored
    withJunk.add( std::numeric_limits<double>::quiet_NaN(), 0.3 ); // ignored
    CHECK( withJunk.count() == 40 );
    REQUIRE( withJunk.fitCFactor( 30, &c ) );
    CHECK( c > 0.0 );
}

// ---------------------------------------------------------------------------
// Operator E2E (rs:brdf_normalization): constant scene, ratio semantics,
// state preservation, angle-refusal contract.
// ---------------------------------------------------------------------------

#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

namespace
{
/// Stamps a dataset metadata key on an existing raster (GA_Update, the same
/// in-place pattern the solar operator uses).
bool stampMetadata( const QString &path, const char *key, double value )
{
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    if ( !ds )
        return false;
    GDALSetMetadataItem( ds, key, QString::number( value, 'g', 10 ).toUtf8().constData(),
                         nullptr );
    GDALClose( ds );
    return true;
}
} // namespace

TEST_CASE( "rs:brdf_normalization E2E: constant scene maps by the kernel ratio",
           "[brdf][operator][e2e]" )
{
    using namespace sicnu::operators;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString reflPath = dir.filePath( "refl.tif" );
    const QString outPath = dir.filePath( "normalized.tif" );

    constexpr int kW = 16;
    constexpr int kH = 12;
    constexpr float kValue = 0.42f;
    sicnu::testing::RsSyntheticRasterBuilder builder( kW, kH, 2 );
    builder.withCrs( "EPSG:32650" );
    builder.withGeoTransform( 0.0, 30.0, 540.0, -30.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
        {
            builder.withPixel( 1, x, y, kValue );
            builder.withPixel( 2, x, y, kValue * 0.5f );
        }
    REQUIRE( !builder.writeToDisk( reflPath ).isEmpty() );
    // Angles come from metadata (the solar-stamp path); weights from params.
    REQUIRE( stampMetadata( reflPath, "SICNU_SUN_ZENITH", 35.0 ) );
    REQUIRE( stampMetadata( reflPath, "SICNU_SUN_AZIMUTH", 150.0 ) );
    REQUIRE( stampMetadata( reflPath, "SICNU_VIEW_ZENITH", 20.0 ) );
    REQUIRE( stampMetadata( reflPath, "SICNU_VIEW_AZIMUTH", 100.0 ) );
    {
        // Write the radiometric-state string through GDAL directly.
        GDALAllRegister();
        GDALDatasetH ds = GDALOpen( reflPath.toUtf8().constData(), GA_Update );
        REQUIRE( ds != nullptr );
        GDALSetMetadataItem( ds, SatelliteProducts::kRadiometricStateKey,
                             SatelliteProducts::kRadiometricStateSurfaceReflectance, nullptr );
        GDALClose( ds );
    }

    auto op = RSOperatorRegistry::instance().create( "rs:brdf_normalization" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = reflPath.toStdString();
    params["output"] = outPath.toStdString();
    params["f_vol"] = 0.3;
    params["f_geo"] = 0.5;

    RSOperatorContext context;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    REQUIRE( result["bandCount"].asInt() == 2 );
    CHECK( result["relative_azimuth"].asDouble() == Catch::Approx( 50.0 ) );

    // Expected: value·f(ref)/f(obs) with ref = (sun 35°, view 0°, Δφ 0),
    // computed through the independently-pinned kernel functions.
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( outPath ) );
    std::vector<float> pixels( static_cast<size_t>( kW ) * kH );
    REQUIRE( outDs.readBandWindow( 1, 0, 0, kW, kH, pixels.data() ) );

    const double fObs = 1.0 + 0.3 * rossThick( 35.0, 20.0, 50.0 )
                        + 0.5 * liSparseReciprocal( 35.0, 20.0, 50.0 );
    const double fRef = 1.0 + 0.3 * rossThick( 35.0, 0.0, 0.0 )
                        + 0.5 * liSparseReciprocal( 35.0, 0.0, 0.0 );
    for ( const float v : pixels )
        CHECK( v == Catch::Approx( kValue * fRef / fObs ).epsilon( 1e-5 ) );

    // The radiometric state must be preserved (normalization keeps units).
    GDALDatasetH ds = GDALOpen( outPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    const char *state =
        GDALGetMetadataItem( ds, SatelliteProducts::kRadiometricStateKey, nullptr );
    REQUIRE( state != nullptr );
    CHECK( QString::fromUtf8( state )
           == QLatin1String( SatelliteProducts::kRadiometricStateSurfaceReflectance ) );
    GDALClose( ds );
}

TEST_CASE( "rs:brdf_normalization refuses missing angles with typed errors",
           "[brdf][operator][e2e]" )
{
    using namespace sicnu::operators;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString reflPath = dir.filePath( "refl.tif" );
    sicnu::testing::RsSyntheticRasterBuilder builder( 8, 8, 1 );
    builder.withPixel( 1, 0, 0, 0.5f );
    REQUIRE( !builder.writeToDisk( reflPath ).isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:brdf_normalization" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = reflPath.toStdString();
    params["output"] = dir.filePath( "out.tif" ).toStdString();
    params["f_vol"] = 0.3;
    params["f_geo"] = 0.5;
    // No angles anywhere: the refusal must name the missing angle metadata.
    RSOperatorContext context;
    try
    {
        (void)op->run( params, context );
        FAIL( "expected RSOperatorError" );
    }
    catch ( const RSOperatorError &e )
    {
        const std::string msg = e.what();
        INFO( msg );
        CHECK( msg.find( "SICNU_SUN_ZENITH" ) != std::string::npos );
    }

    // Below-horizon sun (zenith > 90) is refused, not clamped.
    params["sun_zenith"] = 95.0;
    params["sun_azimuth"] = 150.0;
    params["view_zenith"] = 0.0;
    params["view_azimuth"] = 0.0;
    REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
}
