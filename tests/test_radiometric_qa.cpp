// test_radiometric_qa.cpp — radiometric-physics-11 work package F.
//
// Oracles: hand-computed flag expectations on explicit buffers (every flag
// value is pinned against the frozen vocabulary), hand-computed variance
// algebra for the linear propagation (σy² = (g·σx)² + (x·σg)² + σb² with
// numbers worked in the comments), and mask/QA propagation semantics
// (union-only, never clearing). Negative tests: null buffers, negative
// uncertainty, aliasing no-op.

#include "processing/algorithms/radiometric_qa.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace RadiometricQa;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
} // namespace

TEST_CASE( "flag vocabulary values are frozen", "[qa]" )
{
    CHECK( FlagNone == 0 );
    CHECK( FlagSaturated == 0x0001 );
    CHECK( FlagNegative == 0x0002 );
    CHECK( FlagOverRange == 0x0004 );
    CHECK( FlagNotFinite == 0x0008 );
    CHECK( FlagCloud == 0x0010 );
    CHECK( FlagCloudShadow == 0x0020 );
    CHECK( FlagSnow == 0x0040 );
    CHECK( FlagSaturationQa == 0x0080 );
    CHECK( FlagInvalidAngles == 0x0100 );
}

TEST_CASE( "evaluateReflectance classifies every domain violation", "[qa]" )
{
    const std::vector<float> rho = {
        0.25f,                                   // clean
        -0.01f,                                  // negative only
        1.50f,                                   // over-range only
        kNaN,                                    // not finite
        std::numeric_limits<float>::infinity(),  // not finite
        3.0f,                                    // saturated + over-range
    };
    std::vector<uint16_t> flags( rho.size() );
    Summary s;
    evaluateReflectance( rho.data(), flags.data(), rho.size(), 2.0f, &s );

    CHECK( flags[0] == FlagNone );
    CHECK( flags[1] == FlagNegative );
    CHECK( flags[2] == FlagOverRange );
    CHECK( flags[3] == FlagNotFinite );
    CHECK( flags[4] == FlagNotFinite );
    CHECK( flags[5] == ( FlagSaturated | FlagOverRange ) );

    REQUIRE( s.pixels == 6 );
    CHECK( s.counts[0] == 1 ); // saturated
    CHECK( s.counts[1] == 1 ); // negative
    CHECK( s.counts[2] == 2 ); // over-range (1.5 and 3.0)
    CHECK( s.counts[3] == 2 ); // not finite
    CHECK( s.flaggedPixels == 5 );
    CHECK( s.fraction( FlagNegative ) == Catch::Approx( 1.0 / 6.0 ) );
    CHECK( s.fraction( FlagCloudShadow ) == 0.0 );

    // saturationLevel <= 0 disables threshold saturation (QA-bit saturation
    // still available separately).
    evaluateReflectance( rho.data(), flags.data(), rho.size(), 0.0f, nullptr );
    CHECK( flags[5] == FlagOverRange );
}

TEST_CASE( "propagation unions and never clears", "[qa]" )
{
    std::vector<uint16_t> flags = { FlagNegative, FlagNone, FlagCloud };
    const std::vector<uint16_t> step = { FlagCloudShadow, FlagOverRange, FlagNone };
    propagate( flags.data(), step.data(), flags.size() );

    CHECK( flags[0] == ( FlagNegative | FlagCloudShadow ) );
    CHECK( flags[1] == FlagOverRange );
    CHECK( flags[2] == FlagCloud ); // a clean step clears nothing

    // Aliased call is a documented no-op.
    propagate( flags.data(), flags.data(), flags.size() );
    CHECK( flags[0] == ( FlagNegative | FlagCloudShadow ) );
}

TEST_CASE( "mask and QA saturation propagation", "[qa]" )
{
    std::vector<uint16_t> flags( 4, FlagNone );
    const std::vector<uint8_t> mask = { 0, 1, 1, 0 };

    markFromMask( flags.data(), mask.data(), flags.size(), FlagCloudShadow );
    CHECK( flags[0] == FlagNone );
    CHECK( flags[1] == FlagCloudShadow );
    CHECK( flags[2] == FlagCloudShadow );
    CHECK( flags[3] == FlagNone );

    // Landsat QA_RADSAT: bit 4 = band-4 saturation inside the QA word.
    const std::vector<uint16_t> qaRadsat = { 0x0000, 0x0010, 0x0002, 0xFFFF };
    addSaturationBits( flags.data(), qaRadsat.data(), flags.size(), 1u << 4 );
    CHECK( flags[0] == FlagNone );
    CHECK( flags[1] == ( FlagCloudShadow | FlagSaturationQa ) );
    CHECK( flags[2] == FlagCloudShadow ); // unrelated QA bit ignored
    CHECK( flags[3] == FlagSaturationQa );
}

TEST_CASE( "linear uncertainty propagation matches hand-computed algebra", "[qa]" )
{
    // y = 2.0·x + 0.5 with σ_gain = 0.05, σ_bias = 0.01, σx given per pixel.
    // x = 1.0, σx = 0.1:  σy = sqrt((2·0.1)² + (1·0.05)² + 0.01²)
    //                          = sqrt(0.04 + 0.0025 + 0.0001) = 0.20760
    // x = 2.0, σx = 0  :  σy = sqrt(0 + 0.01 + 0.0001)   = 0.10050
    // x = NaN          :  σy = NaN (input has no defined uncertainty)
    const std::vector<float> x = { 1.0f, 2.0f, kNaN };
    const std::vector<float> sx = { 0.1f, 0.0f, 0.0f };
    std::vector<float> sy( x.size() );

    REQUIRE( propagateLinearUncertainty( x.data(), sx.data(), x.size(), 2.0, 0.05, 0.01,
                                         sy.data() ) );
    CHECK( sy[0] == Catch::Approx( 0.20760 ).margin( 1e-5 ) );
    CHECK( sy[1] == Catch::Approx( 0.10050 ).margin( 1e-5 ) );
    CHECK( std::isnan( sy[2] ) );

    // Zero uncertainties collapse to the pure gain-scaled input error.
    std::vector<float> sy2( 1 );
    REQUIRE( propagateLinearUncertainty( x.data(), nullptr, 1, 3.0, 0.0, 0.0, sy2.data() ) );
    CHECK( sy2[0] == Catch::Approx( 3.0 ).margin( 1e-6 ) );
}

TEST_CASE( "uncertainty propagation refuses bad arguments", "[qa]" )
{
    std::vector<float> x = { 1.0f };
    std::vector<float> sy( 1 );
    QString err;

    CHECK_FALSE( propagateLinearUncertainty( nullptr, nullptr, 1, 1.0, 0, 0, sy.data(), &err ) );
    CHECK_FALSE( propagateLinearUncertainty( x.data(), nullptr, 1, 1.0, 0, 0, nullptr, &err ) );
    CHECK_FALSE( propagateLinearUncertainty( x.data(), nullptr, 1, 1.0, -0.1, 0, sy.data(), &err ) );
    CHECK_FALSE( propagateLinearUncertainty( x.data(), nullptr, 1, -1.0, 0, -0.5, sy.data(), &err ) );
    CHECK( err.contains( QLatin1String( "uncertainty" ) ) );
}

// ---------------------------------------------------------------------------
// Operator E2E (rs:radiometric_qa): flag-band generation, mask propagation,
// grid-mismatch refusal, summary fractions.
// ---------------------------------------------------------------------------

#include <QTemporaryDir>

#include <json/json.h>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

TEST_CASE( "rs:radiometric_qa E2E: flags, mask propagation and summary",
           "[qa][operator][e2e]" )
{
    using namespace sicnu::operators;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString reflPath = dir.filePath( "refl.tif" );
    const QString maskPath = dir.filePath( "mask.tif" );
    const QString outPath = dir.filePath( "qa.tif" );

    constexpr int kW = 10;
    constexpr int kH = 8;
    sicnu::testing::RsSyntheticRasterBuilder refl( kW, kH, 2 );
    refl.withCrs( "EPSG:32650" );
    refl.withGeoTransform( 0.0, 30.0, 240.0, -30.0 );
    // Band 1: clean except one negative + one over-range pixel.
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            refl.withPixel( 1, x, y, 0.35f );
    refl.withPixel( 1, 2, 1, -0.05f );
    refl.withPixel( 1, 3, 1, 1.40f );
    // Band 2: clean except one saturated pixel (level 2.0).
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            refl.withPixel( 2, x, y, 0.60f );
    refl.withPixel( 2, 4, 2, 2.50f );
    REQUIRE( !refl.writeToDisk( reflPath ).isEmpty() );

    sicnu::testing::RsSyntheticRasterBuilder mask( kW, kH, 1, GDT_Byte );
    mask.withCrs( "EPSG:32650" );
    mask.withGeoTransform( 0.0, 30.0, 240.0, -30.0 );
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            mask.withPixel( 1, x, y, 0.0f );
    mask.withPixel( 1, 0, 0, 1.0f );
    mask.withPixel( 1, 9, 7, 1.0f );
    REQUIRE( !mask.writeToDisk( maskPath ).isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:radiometric_qa" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = reflPath.toStdString();
    params["output"] = outPath.toStdString();
    params["saturation_level"] = 2.0;
    params["cloud_mask"] = maskPath.toStdString();
    params["mask_flag"] = "cloud";

    RSOperatorContext context;
    Json::Value result;
    REQUIRE_NOTHROW( result = op->run( params, context ) );
    REQUIRE( result["bandCount"].asInt() == 2 );
    CHECK( result["bands"]["band_1"]["flagged_pixels"].asUInt64() == 4 ); // neg+over+2 cloud
    CHECK( result["bands"]["band_2"]["flagged_pixels"].asUInt64() == 3 ); // sat + 2 cloud
    CHECK( result["bands"]["band_1"]["flagged_fraction"].asDouble()
           == Catch::Approx( 4.0 / ( kW * kH ) ) );

    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( outPath ) );
    REQUIRE( outDs.bandCount() == 2 );
    // Read the uint16 flag bands directly through GDAL.
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( outPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    const auto readBand = [&]( int band, std::vector<uint16_t> &buf ) {
        buf.assign( static_cast<size_t>( kW ) * kH, 0 );
        return GDALRasterIO( GDALGetRasterBand( ds, band ), GF_Read, 0, 0, kW, kH,
                             buf.data(), kW, kH, GDT_UInt16, 0, 0 ) == CE_None;
    };
    std::vector<uint16_t> b1, b2;
    REQUIRE( readBand( 1, b1 ) );
    REQUIRE( readBand( 2, b2 ) );
    GDALClose( ds );

    const auto at = []( const std::vector<uint16_t> &v, int x, int y ) {
        return v[static_cast<size_t>( y ) * kW + x];
    };
    CHECK( at( b1, 0, 0 ) == RadiometricQa::FlagCloud );
    CHECK( at( b1, 2, 1 ) == RadiometricQa::FlagNegative );
    CHECK( at( b1, 3, 1 ) == RadiometricQa::FlagOverRange );
    CHECK( at( b1, 5, 5 ) == RadiometricQa::FlagNone );
    CHECK( at( b2, 4, 2 ) == RadiometricQa::FlagSaturated );
    CHECK( at( b2, 9, 7 ) == RadiometricQa::FlagCloud );
}

TEST_CASE( "rs:radiometric_qa refuses mismatched mask grids", "[qa][operator][e2e]" )
{
    using namespace sicnu::operators;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString reflPath = dir.filePath( "refl.tif" );
    const QString maskPath = dir.filePath( "mask.tif" );

    sicnu::testing::RsSyntheticRasterBuilder refl( 8, 8, 1 );
    refl.withCrs( "EPSG:32650" );
    refl.withGeoTransform( 0.0, 30.0, 240.0, -30.0 );
    REQUIRE( !refl.writeToDisk( reflPath ).isEmpty() );

    sicnu::testing::RsSyntheticRasterBuilder mask( 8, 8, 1 );
    mask.withCrs( "EPSG:4326" ); // different CRS → blocking refusal
    REQUIRE( !mask.writeToDisk( maskPath ).isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:radiometric_qa" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = reflPath.toStdString();
    params["output"] = dir.filePath( "qa.tif" ).toStdString();
    params["cloud_mask"] = maskPath.toStdString();
    RSOperatorContext context;
    REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
}
