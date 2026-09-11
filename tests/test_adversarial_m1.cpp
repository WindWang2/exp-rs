// tests/test_adversarial_m1.cpp
// Adversarial Empirical Stress Suite for Milestone 1 (#773, #806, #783, #785, #801, #803)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/topographic_correction.h"
#include "processing/algorithms/terrain_flow.h"
#include "processing/algorithms/sar/sar_terrain_geometry.h"
#include "processing/algorithms/spectral_indices.h"

namespace SpectralIndices {
bool evi(const float *nir, const float *red, const float *blue, float *out, size_t count, bool isScaled);
bool savi(const float *nir, const float *red, float *out, size_t count, bool isScaled);
bool msavi(const float *nir, const float *red, float *out, size_t count, bool isScaled);
bool evi2(const float *nir, const float *red, float *out, size_t count, bool isScaled);
bool bai(const float *red, const float *nir, float *out, size_t count, bool isScaled);
}

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <cpl_conv.h>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>
#include <limits>
#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;
using Catch::Approx;

namespace {
constexpr float kFloatNaN = std::numeric_limits<float>::quiet_NaN();
constexpr double kDoubleNaN = std::numeric_limits<double>::quiet_NaN();
} // namespace

// ============================================================================
// 1. Minnaert Slope Stability Across Boundary Angles (#773, #806)
// ============================================================================

TEST_CASE( "Adversarial: Minnaert slope recovery across wide k spectrum",
           "[m1][adversarial][topo][minnaert][issue773][issue806]" )
{
    // Test across wide physical and edge k values: 0.01, 0.1, 0.5, 0.85, 1.0, 1.5
    for ( double kTrue : { 0.01, 0.05, 0.1, 0.3, 0.5, 0.7, 0.85, 1.0, 1.5, 2.0 } )
    {
        TopographicCorrection::MinnaertRegression mr;
        for ( double ci : { 0.15, 0.25, 0.40, 0.60, 0.80, 0.95 } )
        {
            const double radiance = 100.0 * std::pow( ci, kTrue );
            mr.add( ci, radiance );
        }
        double kFit = 0.0;
        REQUIRE( mr.fit( &kFit ) );
        CHECK( kFit == Approx( kTrue ).margin( 1e-4 ) );
        CHECK( kFit > 0.0 );
    }
}

TEST_CASE( "Adversarial: Minnaert regression refuses non-physical / singular data",
           "[m1][adversarial][topo][minnaert][refusal]" )
{
    // Case 1: Inverted slope (L inversely proportional to cosi, k < 0)
    {
        TopographicCorrection::MinnaertRegression mr;
        for ( double ci : { 0.2, 0.4, 0.6, 0.8 } )
            mr.add( ci, 10.0 / ci ); // Negative slope in log-log
        double k = 0.0;
        CHECK_FALSE( mr.fit( &k ) );
    }

    // Case 2: Constant illumination (flat terrain, sxx == 0)
    {
        TopographicCorrection::MinnaertRegression mr;
        mr.add( 0.5, 10.0 );
        mr.add( 0.5, 20.0 );
        mr.add( 0.5, 30.0 );
        double k = 0.0;
        CHECK_FALSE( mr.fit( &k ) );
    }

    // Case 3: Fewer than 2 valid points
    {
        TopographicCorrection::MinnaertRegression mr;
        mr.add( 0.5, 10.0 );
        double k = 0.0;
        CHECK_FALSE( mr.fit( &k ) );
    }

    // Case 4: Non-positive / non-finite inputs must be safely ignored
    {
        TopographicCorrection::MinnaertRegression mr;
        mr.add( -0.5, 10.0 );
        mr.add( 0.0, 10.0 );
        mr.add( 0.5, -5.0 );
        mr.add( 0.5, 0.0 );
        mr.add( kDoubleNaN, 10.0 );
        mr.add( 0.5, std::numeric_limits<double>::infinity() );
        double k = 0.0;
        CHECK_FALSE( mr.fit( &k ) ); // Count is 0
    }
}

TEST_CASE( "Adversarial: Minnaert correction pixel behavior at boundary illumination",
           "[m1][adversarial][topo][minnaert][pixel]" )
{
    TopographicCorrection::BandFit fit;
    fit.usable = true;
    fit.cosZenith = std::cos( 30.0 * M_PI / 180.0 );
    fit.k = 0.75;

    // Boundary 1: Self-shadowed (cosi <= 1e-9) -> NaN
    CHECK( std::isnan( TopographicCorrection::correctPixel(
        TopographicCorrection::Method::Minnaert, 50.0f, 1e-9, fit ) ) );
    CHECK( std::isnan( TopographicCorrection::correctPixel(
        TopographicCorrection::Method::Minnaert, 50.0f, 0.0, fit ) ) );
    CHECK( std::isnan( TopographicCorrection::correctPixel(
        TopographicCorrection::Method::Minnaert, 50.0f, -0.3, fit ) ) );

    // Boundary 2: Non-positive radiance -> NaN
    CHECK( std::isnan( TopographicCorrection::correctPixel(
        TopographicCorrection::Method::Minnaert, 0.0f, 0.5, fit ) ) );
    CHECK( std::isnan( TopographicCorrection::correctPixel(
        TopographicCorrection::Method::Minnaert, -10.0f, 0.5, fit ) ) );

    // Boundary 3: Just above 1e-9 threshold -> finite
    const float valNearZero = TopographicCorrection::correctPixel(
        TopographicCorrection::Method::Minnaert, 50.0f, 1e-8, fit );
    CHECK( std::isfinite( valNearZero ) );

    // Boundary 4: k = 1.0 reduces identically to Cosine correction
    fit.k = 1.0;
    for ( double cosi : { 0.1, 0.3, 0.5, 0.8, 1.0 } )
    {
        const float valMinnaert = TopographicCorrection::correctPixel(
            TopographicCorrection::Method::Minnaert, 100.0f, cosi, fit );
        const float valCosine = TopographicCorrection::correctPixel(
            TopographicCorrection::Method::Cosine, 100.0f, cosi, fit );
        CHECK( valMinnaert == Approx( valCosine ).margin( 1e-5f ) );
    }
}

// ============================================================================
// 2. DEM Flow Accumulation Edge Cases (#783)
// ============================================================================

TEST_CASE( "Adversarial: DEM flow accumulation all-NoData raster",
           "[m1][adversarial][terrain][flow][all_nodata][issue783]" )
{
    constexpr int W = 5;
    constexpr int H = 5;
    constexpr float kNodata = -9999.0f;
    std::vector<float> dem( W * H, kNodata );
    std::vector<float> filled( W * H, 0.0f );
    std::vector<float> dir( W * H, 0.0f );
    std::vector<float> acc( W * H, 0.0f );

    REQUIRE( TerrainFlow::fillDepressions( dem.data(), filled.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), W, H ) );

    for ( size_t i = 0; i < W * H; ++i )
    {
        CHECK( filled[i] == kNodata );
        CHECK( dir[i] == kNodata );
        CHECK( acc[i] == kNodata ); // MUST remain NoData! Never 1.0f!
    }
}

TEST_CASE( "Adversarial: DEM flow accumulation all-NaN raster",
           "[m1][adversarial][terrain][flow][all_nan][issue783]" )
{
    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<float> dem( W * H, kFloatNaN );
    std::vector<float> filled( W * H, 0.0f );
    std::vector<float> dir( W * H, 0.0f );
    std::vector<float> acc( W * H, 0.0f );

    REQUIRE( TerrainFlow::fillDepressions( dem.data(), filled.data(), W, H, kFloatNaN ) );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), W, H, kFloatNaN ) );
    REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), W, H ) );

    for ( size_t i = 0; i < W * H; ++i )
    {
        CHECK( std::isnan( filled[i] ) );
        CHECK( std::isnan( dir[i] ) );
        CHECK( std::isnan( acc[i] ) );
    }
}

TEST_CASE( "Adversarial: DEM flow accumulation endorheic basin enclosed by NoData",
           "[m1][adversarial][terrain][flow][isolated_basin][issue783]" )
{
    // 5x5 grid with outer ring NoData, inner 3x3 sloping toward center (2, 2)
    constexpr int W = 5;
    constexpr int H = 5;
    constexpr float kNodata = -9999.0f;
    std::vector<float> dem( W * H, kNodata );

    // Fill inner 3x3 bowl
    // (1,1): 10, (2,1): 8, (3,1): 10
    // (1,2):  8, (2,2): 2, (3,2):  8
    // (1,3): 10, (2,3): 8, (3,3): 10
    dem[1 * W + 1] = 10.0f; dem[1 * W + 2] = 8.0f; dem[1 * W + 3] = 10.0f;
    dem[2 * W + 1] =  8.0f; dem[2 * W + 2] = 2.0f; dem[2 * W + 3] =  8.0f;
    dem[3 * W + 1] = 10.0f; dem[3 * W + 2] = 8.0f; dem[3 * W + 3] = 10.0f;

    std::vector<float> filled( W * H, 0.0f );
    std::vector<float> dir( W * H, 0.0f );
    std::vector<float> acc( W * H, 0.0f );

    REQUIRE( TerrainFlow::fillDepressions( dem.data(), filled.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), W, H ) );

    // Center cell (2, 2) is a fill flat: #848 (Scientific Algorithms 9.0)
    // seeds the priority-flood on NoData-adjacent valid cells, so the basin
    // fills to the spill elevation of its lowest rim cell (8) instead of
    // silently keeping the raw pit. The filled rim equals the filled centre,
    // so every interior cell terminates as its own D8 sink; the centre no
    // longer hoards the basin's accumulation (water spills across the
    // NoData rim at the seed's own elevation).
    const size_t centerIdx = 2 * W + 2;
    CHECK( filled[centerIdx] == Approx( 8.0f ) );
    CHECK( dir[centerIdx] == 0.0f );
    CHECK( acc[centerIdx] == Approx( 1.0f ) );
    for ( size_t i = 0; i < W * H; ++i )
    {
        if ( dem[i] != kNodata )
            CHECK( acc[i] >= 1.0f );
    }

    // Outer ring must remain NoData throughout
    for ( int y = 0; y < H; ++y )
    {
        for ( int x = 0; x < W; ++x )
        {
            if ( x == 0 || y == 0 || x == W - 1 || y == H - 1 )
            {
                const size_t idx = y * W + x;
                CHECK( dir[idx] == kNodata );
                CHECK( acc[idx] == kNodata );
            }
        }
    }
}

TEST_CASE( "Adversarial: DEM flow accumulation on flat plateau",
           "[m1][adversarial][terrain][flow][flat_plateau]" )
{
    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<float> dem( W * H, 100.0f ); // Completely flat
    std::vector<float> filled( W * H, 0.0f );
    std::vector<float> dir( W * H, 0.0f );
    std::vector<float> acc( W * H, 0.0f );

    REQUIRE( TerrainFlow::fillDepressions( dem.data(), filled.data(), W, H, -9999.0f ) );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), W, H, -9999.0f ) );
    REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), W, H ) );

    // Every flat cell has no lower neighbor -> dir = 0 (sink) -> acc = 1.0f
    for ( size_t i = 0; i < W * H; ++i )
    {
        CHECK( dir[i] == 0.0f );
        CHECK( acc[i] == 1.0f );
    }
}

// ============================================================================
// 3. SAR Look Azimuth Modulo Arithmetic Across 0/360 Boundary (#785)
// ============================================================================

TEST_CASE( "Adversarial: SAR terrain geometry 360-degree modulo periodicity",
           "[m1][adversarial][sar][geometry][modulo][issue785]" )
{
    constexpr double incidenceDeg = 35.0;
    constexpr double dzdx = 0.15;
    constexpr double dzdy = -0.20;

    // Look azimuth at 0 deg vs 360 deg vs 720 deg vs -360 deg
    const auto g0   = sicnu::sar::terrainGeometry( dzdx, dzdy, incidenceDeg, 0.0 );
    const auto g360 = sicnu::sar::terrainGeometry( dzdx, dzdy, incidenceDeg, 360.0 );
    const auto g720 = sicnu::sar::terrainGeometry( dzdx, dzdy, incidenceDeg, 720.0 );
    const auto gNeg = sicnu::sar::terrainGeometry( dzdx, dzdy, incidenceDeg, -360.0 );

    REQUIRE( std::isfinite( g0.localIncidenceDeg ) );
    CHECK( g360.localIncidenceDeg == Approx( g0.localIncidenceDeg ).margin( 1e-7 ) );
    CHECK( g720.localIncidenceDeg == Approx( g0.localIncidenceDeg ).margin( 1e-7 ) );
    CHECK( gNeg.localIncidenceDeg == Approx( g0.localIncidenceDeg ).margin( 1e-7 ) );
    CHECK( g0.maskClass == g360.maskClass );
    CHECK( g0.maskClass == g720.maskClass );
    CHECK( g0.maskClass == gNeg.maskClass );

    // Look azimuth at 90 deg vs 450 deg vs -270 deg
    const auto g90  = sicnu::sar::terrainGeometry( dzdx, dzdy, incidenceDeg, 90.0 );
    const auto g450 = sicnu::sar::terrainGeometry( dzdx, dzdy, incidenceDeg, 450.0 );
    const auto gM270 = sicnu::sar::terrainGeometry( dzdx, dzdy, incidenceDeg, -270.0 );

    REQUIRE( std::isfinite( g90.localIncidenceDeg ) );
    CHECK( g450.localIncidenceDeg == Approx( g90.localIncidenceDeg ).margin( 1e-7 ) );
    CHECK( gM270.localIncidenceDeg == Approx( g90.localIncidenceDeg ).margin( 1e-7 ) );
    CHECK( g90.maskClass == g450.maskClass );
}

TEST_CASE( "Adversarial: SAR terrain masks operator heading to look azimuth conversion across boundary",
           "[m1][adversarial][sar][operator][heading_boundary][issue785]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString demPath = dir.filePath( "dem.tif" );

    constexpr int W = 8;
    constexpr int H = 8;
    RsSyntheticRasterBuilder b( W, H, 1, GDT_Float32 );
    for ( int y = 0; y < H; ++y )
        for ( int x = 0; x < W; ++x )
            b.withPixel( 1, x, y, static_cast<float>( 500.0 + 10.0 * x - 5.0 * y ) );
    REQUIRE( !b.writeToDisk( demPath ).isEmpty() );

    auto op = RSOperatorRegistry::instance().create( "rs:sar_terrain_masks" );
    REQUIRE( op != nullptr );
    RSOperatorContext context;

    // Test 1: heading = 270 deg, right-looking -> look azimuth = (270 + 90) % 360 = 0 deg
    Json::Value p1( Json::objectValue );
    p1["dem"] = demPath.toStdString();
    p1["output"] = dir.filePath( "mask_270_right.tif" ).toStdString();
    p1["product"] = "local_incidence";
    p1["incidence"] = 35.0;
    p1["heading"] = 270.0;
    p1["lookDirection"] = "right";
    Json::Value res1 = op->run( p1, context );
    CHECK( res1["lookAzimuthDeg"].asDouble() == Approx( 0.0 ).margin( 1e-5 ) );

    // Test 2: heading = 90 deg, left-looking -> look azimuth = (90 - 90) % 360 = 0 deg
    Json::Value p2( Json::objectValue );
    p2["dem"] = demPath.toStdString();
    p2["output"] = dir.filePath( "mask_90_left.tif" ).toStdString();
    p2["product"] = "local_incidence";
    p2["incidence"] = 35.0;
    p2["heading"] = 90.0;
    p2["lookDirection"] = "left";
    Json::Value res2 = op->run( p2, context );
    CHECK( res2["lookAzimuthDeg"].asDouble() == Approx( 0.0 ).margin( 1e-5 ) );

    // Test 3: heading = 0 deg, left-looking -> look azimuth = (0 - 90) % 360 = 270 deg
    Json::Value p3( Json::objectValue );
    p3["dem"] = demPath.toStdString();
    p3["output"] = dir.filePath( "mask_0_left.tif" ).toStdString();
    p3["product"] = "local_incidence";
    p3["incidence"] = 35.0;
    p3["heading"] = 0.0;
    p3["lookDirection"] = "left";
    Json::Value res3 = op->run( p3, context );
    CHECK( res3["lookAzimuthDeg"].asDouble() == Approx( 270.0 ).margin( 1e-5 ) );

    // Outputs for p1 and p2 must be identical because both produce lookAzimuth = 0 deg
    GdalDatasetWrapper ds1, ds2;
    REQUIRE( ds1.open( dir.filePath( "mask_270_right.tif" ) ) );
    REQUIRE( ds2.open( dir.filePath( "mask_90_left.tif" ) ) );
    std::vector<float> b1( W * H ), b2( W * H );
    REQUIRE( ds1.readBandData( 1, b1.data(), W, H ) );
    REQUIRE( ds2.readBandData( 1, b2.data(), W, H ) );
    for ( size_t i = 0; i < W * H; ++i )
    {
        CHECK( b1[i] == Approx( b2[i] ).margin( 1e-5f ) );
    }
}

// ============================================================================
// 4. Spectral Index Scale Stability and Sentinel Propagation (#801)
// ============================================================================

TEST_CASE( "Adversarial: Spectral indices scale consistency between [0, 1] and [0, 10000]",
           "[m1][adversarial][spectral][scale][issue801]" )
{
    constexpr size_t N = 4;
    // Reflectance domain [0, 1]
    const std::vector<float> nirRef = { 0.40f, 0.60f, 0.25f, 0.50f };
    const std::vector<float> redRef = { 0.10f, 0.15f, 0.05f, 0.20f };
    const std::vector<float> blueRef = { 0.05f, 0.08f, 0.02f, 0.10f };

    // DN domain [0, 10000]
    std::vector<float> nirDN( N ), redDN( N ), blueDN( N );
    for ( size_t i = 0; i < N; ++i )
    {
        nirDN[i] = nirRef[i] * 10000.0f;
        redDN[i] = redRef[i] * 10000.0f;
        blueDN[i] = blueRef[i] * 10000.0f;
    }

    // Test EVI
    std::vector<float> eviRef( N ), eviDN( N );
    REQUIRE( SpectralIndices::evi( nirRef.data(), redRef.data(), blueRef.data(), eviRef.data(), N, false ) );
    REQUIRE( SpectralIndices::evi( nirDN.data(), redDN.data(), blueDN.data(), eviDN.data(), N, true ) );
    for ( size_t i = 0; i < N; ++i )
        CHECK( eviRef[i] == Approx( eviDN[i] ).margin( 1e-4f ) );

    // Test SAVI
    std::vector<float> saviRef( N ), saviDN( N );
    REQUIRE( SpectralIndices::savi( nirRef.data(), redRef.data(), saviRef.data(), N, false ) );
    REQUIRE( SpectralIndices::savi( nirDN.data(), redDN.data(), saviDN.data(), N, true ) );
    for ( size_t i = 0; i < N; ++i )
        CHECK( saviRef[i] == Approx( saviDN[i] ).margin( 1e-4f ) );

    // Test MSAVI
    std::vector<float> msaviRef( N ), msaviDN( N );
    REQUIRE( SpectralIndices::msavi( nirRef.data(), redRef.data(), msaviRef.data(), N, false ) );
    REQUIRE( SpectralIndices::msavi( nirDN.data(), redDN.data(), msaviDN.data(), N, true ) );
    for ( size_t i = 0; i < N; ++i )
        CHECK( msaviRef[i] == Approx( msaviDN[i] ).margin( 1e-4f ) );

    // Test EVI2
    std::vector<float> evi2Ref( N ), evi2DN( N );
    REQUIRE( SpectralIndices::evi2( nirRef.data(), redRef.data(), evi2Ref.data(), N, false ) );
    REQUIRE( SpectralIndices::evi2( nirDN.data(), redDN.data(), evi2DN.data(), N, true ) );
    for ( size_t i = 0; i < N; ++i )
        CHECK( evi2Ref[i] == Approx( evi2DN[i] ).margin( 1e-4f ) );

    // Test BAI
    std::vector<float> baiRef( N ), baiDN( N );
    REQUIRE( SpectralIndices::bai( redRef.data(), nirRef.data(), baiRef.data(), N, false ) );
    REQUIRE( SpectralIndices::bai( redDN.data(), nirDN.data(), baiDN.data(), N, true ) );
    for ( size_t i = 0; i < N; ++i )
        CHECK( baiRef[i] == Approx( baiDN[i] ).margin( 1e-4f ) );
}

TEST_CASE( "Adversarial: Spectral index sentinel propagation (NaN output)",
           "[m1][adversarial][spectral][sentinel][issue801]" )
{
    constexpr size_t N = 3;
    const std::vector<float> nir = { kFloatNaN, 0.5f, 0.5f };
    const std::vector<float> red = { 0.2f, kFloatNaN, 0.2f };
    const std::vector<float> blue = { 0.1f, 0.1f, kFloatNaN };

    std::vector<float> eviOut( N );
    REQUIRE( SpectralIndices::evi( nir.data(), red.data(), blue.data(), eviOut.data(), N, false ) );
    CHECK( std::isnan( eviOut[0] ) );
    CHECK( std::isnan( eviOut[1] ) );
    CHECK( std::isnan( eviOut[2] ) );

    std::vector<float> saviOut( N );
    REQUIRE( SpectralIndices::savi( nir.data(), red.data(), saviOut.data(), N, false ) );
    CHECK( std::isnan( saviOut[0] ) );
    CHECK( std::isnan( saviOut[1] ) );
    CHECK( std::isfinite( saviOut[2] ) ); // SAVI does not use blue band
}

TEST_CASE( "Adversarial: Spectral index operator with -32768 NoData on unit reflectance raster",
           "[m1][adversarial][spectral][operator][nodata_probe][issue801]" )
{
    // Stress test the dataset sample probing logic in rs_spectral_index_operator.cpp:
    // If a unit reflectance raster has declared NoData = -32768.0f (standard int16/float sentinel)
    // in the swath corners, does the operator correctly detect unit reflectance OR does
    // std::abs(-32768) > 5.0 falsely trigger DN scale mode?
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString inputPath = dir.filePath( "unit_refl_with_nodata.tif" );
    const QString outputPath = dir.filePath( "savi_out.tif" );

    constexpr int W = 32;
    constexpr int H = 32;
    constexpr float kNodata = -32768.0f;

    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, inputPath.toUtf8().constData(), W, H, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );

    std::vector<float> bNIR( W * H, 0.40f );
    std::vector<float> bRed( W * H, 0.10f );

    // Set corners to NoData sentinel -32768.0f
    bNIR[0] = kNodata;
    bRed[0] = kNodata;
    bNIR[W - 1] = kNodata;
    bRed[W - 1] = kNodata;
    bNIR[( H - 1 ) * W] = kNodata;
    bRed[( H - 1 ) * W] = kNodata;
    bNIR[W * H - 1] = kNodata;
    bRed[W * H - 1] = kNodata;

    GDALRasterBandH hNIR = GDALGetRasterBand( ds, 1 );
    GDALRasterBandH hRed = GDALGetRasterBand( ds, 2 );
    GDALSetRasterNoDataValue( hNIR, kNodata );
    GDALSetRasterNoDataValue( hRed, kNodata );
    REQUIRE( GDALRasterIO( hNIR, GF_Write, 0, 0, W, H, bNIR.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( hRed, GF_Write, 0, 0, W, H, bRed.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );

    auto op = RSOperatorRegistry::instance().create( "rs:spectral_index" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["index"] = "SAVI";
    params["nir"] = 1;
    params["red"] = 2;

    RSOperatorContext ctx;
    Json::Value res = op->run( params, ctx );

    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( outputPath ) );
    std::vector<float> out( W * H );
    REQUIRE( outDs.readBandData( 1, out.data(), W, H ) );

    // Corner was NoData -> must be NaN
    CHECK( std::isnan( out[0] ) );

    // Expected SAVI for NIR=0.4, Red=0.1 on unit reflectance:
    // (0.4 - 0.1) / (0.4 + 0.1 + 0.5) * 1.5 = 0.3 / 1.0 * 1.5 = 0.45
    // If bug is present (isScaledDataset == true due to -32768), L would be 5000 -> SAVI = 0.00006!
    const float validVal = out[W * 2 + 2];
    INFO( "Computed SAVI at valid pixel: " << validVal );
    CHECK( validVal == Approx( 0.45f ).margin( 0.01f ) );
}

// ============================================================================
// 5. Multi-Band SAR Speckle Filtering Disparate Sentinels (#803)
// ============================================================================

TEST_CASE( "Adversarial: Multi-band SAR speckle filtering with 3 disparate sentinels",
           "[m1][adversarial][sar][speckle][disparate_sentinels][issue803]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString b1Path = dir.filePath( "band1.tif" );
    const QString b2Path = dir.filePath( "band2.tif" );
    const QString b3Path = dir.filePath( "band3.tif" );
    const QString vrtPath = dir.filePath( "multiband.vrt" );
    const QString outPath = dir.filePath( "speckle_filtered.tif" );

    constexpr int W = 16;
    constexpr int H = 16;
    constexpr float nodataB1 = -9999.0f;
    constexpr float nodataB2 = -32768.0f;
    // Band 3 has undeclared sentinel (NaN in data)

    std::vector<float> d1( W * H, 100.0f );
    std::vector<float> d2( W * H, 200.0f );
    std::vector<float> d3( W * H, 300.0f );

    // Put distinct sentinels at different pixel coordinates
    d1[2 * W + 2] = nodataB1; // B1 has NoData at (2, 2)
    d2[4 * W + 4] = nodataB2; // B2 has NoData at (4, 4)
    d3[6 * W + 6] = kFloatNaN; // B3 has NaN at (6, 6)

    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );

    // Write B1
    GDALDatasetH h1 = GDALCreate( driver, b1Path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( h1 != nullptr );
    GDALRasterBandH hb1 = GDALGetRasterBand( h1, 1 );
    GDALSetRasterNoDataValue( hb1, nodataB1 );
    REQUIRE( GDALRasterIO( hb1, GF_Write, 0, 0, W, H, d1.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( h1 );

    // Write B2
    GDALDatasetH h2 = GDALCreate( driver, b2Path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( h2 != nullptr );
    GDALRasterBandH hb2 = GDALGetRasterBand( h2, 1 );
    GDALSetRasterNoDataValue( hb2, nodataB2 );
    REQUIRE( GDALRasterIO( hb2, GF_Write, 0, 0, W, H, d2.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( h2 );

    // Write B3 (no nodata metadata declared)
    GDALDatasetH h3 = GDALCreate( driver, b3Path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( h3 != nullptr );
    GDALRasterBandH hb3 = GDALGetRasterBand( h3, 1 );
    REQUIRE( GDALRasterIO( hb3, GF_Write, 0, 0, W, H, d3.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( h3 );

    // Create 3-band VRT
    const QString vrtContent = QString(
        "<VRTDataset rasterXSize=\"%1\" rasterYSize=\"%2\">\n"
        "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n"
        "    <NoDataValue>%3</NoDataValue>\n"
        "    <SimpleSource>\n"
        "      <SourceFilename relativeToVRT=\"1\">band1.tif</SourceFilename>\n"
        "      <SourceBand>1</SourceBand>\n"
        "      <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"%1\" ySize=\"%2\"/>\n"
        "      <DstRect xOff=\"0\" yOff=\"0\" xSize=\"%1\" ySize=\"%2\"/>\n"
        "    </SimpleSource>\n"
        "  </VRTRasterBand>\n"
        "  <VRTRasterBand dataType=\"Float32\" band=\"2\">\n"
        "    <NoDataValue>%4</NoDataValue>\n"
        "    <SimpleSource>\n"
        "      <SourceFilename relativeToVRT=\"1\">band2.tif</SourceFilename>\n"
        "      <SourceBand>1</SourceBand>\n"
        "      <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"%1\" ySize=\"%2\"/>\n"
        "      <DstRect xOff=\"0\" yOff=\"0\" xSize=\"%1\" ySize=\"%2\"/>\n"
        "    </SimpleSource>\n"
        "  </VRTRasterBand>\n"
        "  <VRTRasterBand dataType=\"Float32\" band=\"3\">\n"
        "    <SimpleSource>\n"
        "      <SourceFilename relativeToVRT=\"1\">band3.tif</SourceFilename>\n"
        "      <SourceBand>1</SourceBand>\n"
        "      <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"%1\" ySize=\"%2\"/>\n"
        "      <DstRect xOff=\"0\" yOff=\"0\" xSize=\"%1\" ySize=\"%2\"/>\n"
        "    </SimpleSource>\n"
        "  </VRTRasterBand>\n"
        "</VRTDataset>\n"
    ).arg( W ).arg( H ).arg( nodataB1 ).arg( nodataB2 );

    QFile vrtFile( vrtPath );
    REQUIRE( vrtFile.open( QIODevice::WriteOnly | QIODevice::Text ) );
    vrtFile.write( vrtContent.toUtf8() );
    vrtFile.close();

    auto op = RSOperatorRegistry::instance().create( "rs:sar_speckle" );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = vrtPath.toStdString();
    params["output"] = outPath.toStdString();
    params["method"] = "lee";
    params["kernelSize"] = 3;
    params["band"] = 0; // Filter all bands

    RSOperatorContext ctx;
    Json::Value res = op->run( params, ctx );
    CHECK( res["bands"].asInt() == 3 );

    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( outPath ) );
    std::vector<float> ob1( W * H ), ob2( W * H ), ob3( W * H );
    REQUIRE( outDs.readBandData( 1, ob1.data(), W, H ) );
    REQUIRE( outDs.readBandData( 2, ob2.data(), W, H ) );
    REQUIRE( outDs.readBandData( 3, ob3.data(), W, H ) );

    // Band 1: (2, 2) must be NaN; (4, 4) and (6, 6) must be finite
    CHECK( std::isnan( ob1[2 * W + 2] ) );
    CHECK( std::isfinite( ob1[4 * W + 4] ) );
    CHECK( std::isfinite( ob1[6 * W + 6] ) );

    // Band 2: (4, 4) must be NaN; (2, 2) and (6, 6) must be finite
    CHECK( std::isfinite( ob2[2 * W + 2] ) );
    CHECK( std::isnan( ob2[4 * W + 4] ) );
    CHECK( std::isfinite( ob2[6 * W + 6] ) );

    // Band 3: (6, 6) must be NaN; (2, 2) and (4, 4) must be finite
    CHECK( std::isfinite( ob3[2 * W + 2] ) );
    CHECK( std::isfinite( ob3[4 * W + 4] ) );
    CHECK( std::isnan( ob3[6 * W + 6] ) );
}
