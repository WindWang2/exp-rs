// tests/test_chunked_operators_parity.cpp — Parity Regression Test Suite (Milestone 3)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "operators/framework/chunked_run.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include "operators/rs/rs_change_streaming.h"
#include "processing/algorithms/math_utils.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/framework/output_committer.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "data/data_manager.h"
#include "raster_bit_compare.h"

#include <QTemporaryDir>
#include <QFile>
#include <gdal.h>
#include <gdal_priv.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace sicnu::operators;

namespace {

std::string writeTestRaster( const QString &path, int width, int height,
                             const std::vector<std::vector<float>> &bands,
                             std::optional<double> nodata = std::nullopt,
                             const std::map<std::string, std::string> &meta = {} )
{
    ensureGdalInit();
    std::array<double, 6> geoTransform = { 0, 1, 0, 0, 0, -1 };
    QString error;
    if ( !writeGdalOutput( path, width, height, bands, geoTransform, "EPSG:4326", &error, nodata ) )
    {
        return error.toStdString();
    }
    if ( !meta.empty() )
    {
        GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
        if ( !ds ) return "failed to open raster for metadata update";
        for ( const auto &[k, v] : meta )
        {
            GDALSetMetadataItem( ds, k.c_str(), v.c_str(), nullptr );
        }
        GDALClose( ds );
    }
    return {};
}

void writeReferenceTiff( const QString &path, int width, int height,
                         const float *data, const std::array<double, 6> &gt,
                         const QString &proj,
                         double nodata = std::numeric_limits<double>::quiet_NaN() )
{
    QString err;
    GDALDatasetH ds = createOutputTiff( path, width, height, 1,
                                        static_cast<int>( GDT_Float32 ), gt, proj, &err );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, width, height,
                           const_cast<float *>( data ), width, height, GDT_Float32, 0, 0 ) == CE_None );
    GDALSetRasterNoDataValue( band, nodata );
    GDALClose( ds );
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// P1: NDVI Multi-Tile Parity
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: NDVI Bit-Exact Numerical Equivalence",
           "[operators][rs][chunked][parity][ndvi]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 600;
    constexpr int H = 600; // 3x3 tiles of 256x256
    const QString inPath = tmp.path() + "/in_ndvi.tif";
    const QString chunkedOutPath = tmp.path() + "/out_ndvi_chunked.tif";
    const QString facadeOutPath = tmp.path() + "/out_ndvi_facade.tif";
    const QString anchorOutPath = tmp.path() + "/out_ndvi_anchor.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );

    for ( int i = 0; i < W * H; ++i )
    {
        bands[0][i] = 10.0f + static_cast<float>( i % 17 ); // Blue
        bands[1][i] = 20.0f + static_cast<float>( i % 19 ); // Green
        bands[2][i] = 15.0f + static_cast<float>( i % 23 ); // Red
        bands[3][i] = 45.0f + static_cast<float>( ( i * 7 ) % 31 ); // NIR
    }
    // Inject zero-denominator and nodata pixels
    bands[2][100] = 0.0f; bands[3][100] = 0.0f;
    bands[2][200] = -9999.0f;

    REQUIRE( writeTestRaster( inPath, W, H, bands, -9999.0 ).empty() );

    // Serial reference anchor
    std::vector<float> expected( static_cast<size_t>( W ) * H );
    std::vector<float> nirNorm = bands[3];
    std::vector<float> redNorm = bands[2];
    for ( size_t i = 0; i < expected.size(); ++i )
    {
        if ( nirNorm[i] == -9999.0f || !std::isfinite( nirNorm[i] ) )
            nirNorm[i] = std::numeric_limits<float>::quiet_NaN();
        if ( redNorm[i] == -9999.0f || !std::isfinite( redNorm[i] ) )
            redNorm[i] = std::numeric_limits<float>::quiet_NaN();
    }
    REQUIRE( MathUtils::normalizedDifference( nirNorm.data(), redNorm.data(),
                                             expected.data(), expected.size() ) );

    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( anchorOutPath, W, H, expected.data(), gt, "EPSG:4326" );

    // Run chunked rs:ndvi
    auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = chunkedOutPath.toStdString();
    params["nir"] = 4;
    params["red"] = 3;

    RSOperatorContext ctx;
    Json::Value res = op->run( params, ctx );
    REQUIRE( res["output"].asString() == chunkedOutPath.toStdString() );
    REQUIRE( res["total_tiles"].asUInt64() == 9 );
    REQUIRE( res["tiles_computed"].asUInt64() == 9 );

    // Bit-exact verification against serial anchor
    const auto report = sicnu::testing::compareRastersBitExact(
        chunkedOutPath.toStdString(), anchorOutPath.toStdString() );
    if ( !report.identical )
        FAIL( report.detail );
    REQUIRE( report.identical );

    // Run facade rs:spectral_index with method=NDVI
    auto facadeOp = RSOperatorRegistry::instance().create( "rs:spectral_index" );
    REQUIRE( facadeOp != nullptr );

    Json::Value facadeParams( Json::objectValue );
    facadeParams["input"] = inPath.toStdString();
    facadeParams["output"] = facadeOutPath.toStdString();
    facadeParams["index"] = "NDVI";
    facadeParams["nir"] = 4;
    facadeParams["red"] = 3;

    RSOperatorContext facadeCtx;
    Json::Value facadeRes = facadeOp->run( facadeParams, facadeCtx );
    REQUIRE( facadeRes["output"].asString() == facadeOutPath.toStdString() );

    // Bit-exact alias == facade verification
    const auto facadeReport = sicnu::testing::compareRastersBitExact(
        facadeOutPath.toStdString(), anchorOutPath.toStdString() );
    if ( !facadeReport.identical )
        FAIL( facadeReport.detail );
    REQUIRE( facadeReport.identical );
}

// ---------------------------------------------------------------------------
// P2: NDWI Multi-Tile Parity
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: NDWI Multi-Tile Parity",
           "[operators][rs][chunked][parity][ndwi]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 512;
    constexpr int H = 512; // 2x2 tiles of 256x256
    const QString inPath = tmp.path() + "/in_ndwi.tif";
    const QString chunkedOutPath = tmp.path() + "/out_ndwi_chunked.tif";
    const QString anchorOutPath = tmp.path() + "/out_ndwi_anchor.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );

    for ( int i = 0; i < W * H; ++i )
    {
        bands[1][i] = 30.0f + static_cast<float>( i % 29 ); // Green (band 2)
        bands[3][i] = 50.0f + static_cast<float>( ( i * 5 ) % 37 ); // NIR (band 4)
    }

    REQUIRE( writeTestRaster( inPath, W, H, bands ).empty() );

    // Serial reference: NDWI = (Green - NIR) / (Green + NIR)
    std::vector<float> expected( static_cast<size_t>( W ) * H );
    REQUIRE( MathUtils::normalizedDifference( bands[1].data(), bands[3].data(),
                                             expected.data(), expected.size() ) );

    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( anchorOutPath, W, H, expected.data(), gt, "EPSG:4326" );

    auto op = RSOperatorRegistry::instance().create( "rs:ndwi" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = chunkedOutPath.toStdString();
    params["green"] = 2;
    params["nir"] = 4;

    RSOperatorContext ctx;
    Json::Value res = op->run( params, ctx );
    REQUIRE( res["output"].asString() == chunkedOutPath.toStdString() );
    REQUIRE( res["total_tiles"].asUInt64() == 4 );
    REQUIRE( res["tiles_computed"].asUInt64() == 4 );

    const auto report = sicnu::testing::compareRastersBitExact(
        chunkedOutPath.toStdString(), anchorOutPath.toStdString() );
    if ( !report.identical )
        FAIL( report.detail );
    REQUIRE( report.identical );
}

// ---------------------------------------------------------------------------
// P3: EVI Unit & Stamped Scale
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: EVI Unit & Stamped Scale",
           "[operators][rs][chunked][parity][evi]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 600;
    constexpr int H = 600; // 9 tiles of 256x256

    // Section 1: Unit reflectance
    {
        const QString inPath = tmp.path() + "/in_evi_unit.tif";
        const QString chunkedOutPath = tmp.path() + "/out_evi_unit.tif";
        const QString anchorOutPath = tmp.path() + "/out_evi_unit_anchor.tif";

        std::vector<std::vector<float>> bands( 4 );
        for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );

        for ( int i = 0; i < W * H; ++i )
        {
            bands[0][i] = 0.05f + static_cast<float>( i % 10 ) * 0.01f; // Blue (band 1)
            bands[2][i] = 0.10f + static_cast<float>( i % 15 ) * 0.01f; // Red (band 3)
            bands[3][i] = 0.40f + static_cast<float>( i % 25 ) * 0.01f; // NIR (band 4)
        }
        REQUIRE( writeTestRaster( inPath, W, H, bands ).empty() );

        std::vector<float> expected( static_cast<size_t>( W ) * H );
        REQUIRE( SpectralIndices::eviUnit( bands[3].data(), bands[2].data(), bands[0].data(),
                                           expected.data(), expected.size() ) );

        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( anchorOutPath, W, H, expected.data(), gt, "EPSG:4326" );

        auto op = RSOperatorRegistry::instance().create( "rs:evi" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["input"] = inPath.toStdString();
        params["output"] = chunkedOutPath.toStdString();
        params["nir"] = 4;
        params["red"] = 3;
        params["blue"] = 1;

        RSOperatorContext ctx;
        Json::Value res = op->run( params, ctx );
        REQUIRE( res["numeric_domain"]["regime"].asString() == "unit_reflectance" );

        const auto report = sicnu::testing::compareRastersBitExact(
            chunkedOutPath.toStdString(), anchorOutPath.toStdString() );
        if ( !report.identical )
            FAIL( report.detail );
        REQUIRE( report.identical );
    }

    // Section 2: Stamped scale (SICNU_NUMERIC_SCALE=10000)
    {
        const QString inPath = tmp.path() + "/in_evi_stamped.tif";
        const QString chunkedOutPath = tmp.path() + "/out_evi_stamped.tif";
        const QString anchorOutPath = tmp.path() + "/out_evi_stamped_anchor.tif";

        std::vector<std::vector<float>> bands( 4 );
        for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );

        for ( int i = 0; i < W * H; ++i )
        {
            bands[0][i] = 500.0f + static_cast<float>( i % 100 ); // Blue (band 1)
            bands[2][i] = 1000.0f + static_cast<float>( i % 150 ); // Red (band 3)
            bands[3][i] = 4000.0f + static_cast<float>( i % 250 ); // NIR (band 4)
        }
        REQUIRE( writeTestRaster( inPath, W, H, bands, std::nullopt,
                                  { { "SICNU_NUMERIC_SCALE", "10000" } } ).empty() );

        std::vector<float> nirNorm( static_cast<size_t>( W ) * H );
        std::vector<float> redNorm( static_cast<size_t>( W ) * H );
        std::vector<float> blueNorm( static_cast<size_t>( W ) * H );
        std::vector<float> expected( static_cast<size_t>( W ) * H );
        for ( size_t i = 0; i < expected.size(); ++i )
        {
            nirNorm[i] = bands[3][i] * 0.0001f;
            redNorm[i] = bands[2][i] * 0.0001f;
            blueNorm[i] = bands[0][i] * 0.0001f;
        }
        REQUIRE( SpectralIndices::eviUnit( nirNorm.data(), redNorm.data(), blueNorm.data(),
                                           expected.data(), expected.size() ) );

        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( anchorOutPath, W, H, expected.data(), gt, "EPSG:4326" );

        auto op = RSOperatorRegistry::instance().create( "rs:evi" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["input"] = inPath.toStdString();
        params["output"] = chunkedOutPath.toStdString();
        params["nir"] = 4;
        params["red"] = 3;
        params["blue"] = 1;

        RSOperatorContext ctx;
        Json::Value res = op->run( params, ctx );
        REQUIRE( res["numeric_domain"]["regime"].asString() == "dn_scale" );

        const auto report = sicnu::testing::compareRastersBitExact(
            chunkedOutPath.toStdString(), anchorOutPath.toStdString() );
        if ( !report.identical )
            FAIL( report.detail );
        REQUIRE( report.identical );
    }
}

// ---------------------------------------------------------------------------
// P4: SAVI Scale Probe Invariant (#801)
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: SAVI Scale Probe Invariant (#801)",
           "[operators][rs][chunked][parity][savi]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 300;
    constexpr int H = 600; // Split vertically across 3 tile rows (0..255, 256..511, 512..599)
    constexpr int kSwitchRow = 300;
    const QString inPath = tmp.path() + "/in_savi.tif";
    const QString chunkedOutPath = tmp.path() + "/out_savi_chunked.tif";
    const QString anchorOutPath = tmp.path() + "/out_savi_anchor.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );

    for ( int y = 0; y < H; ++y )
    {
        for ( int x = 0; x < W; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * W + x;
            if ( y < kSwitchRow )
            {
                bands[3][i] = 4000.0f + static_cast<float>( i % 11 ); // NIR
                bands[2][i] = 2000.0f + static_cast<float>( i % 7 );  // Red
            }
            else
            {
                // Small values below DN threshold in lower half
                bands[3][i] = 4.0f + static_cast<float>( i % 2 );
                bands[2][i] = 2.0f + static_cast<float>( i % 3 );
            }
        }
    }
    REQUIRE( writeTestRaster( inPath, W, H, bands ).empty() );

    // Global scale anchor: whole raster scaled by 1/10000.0f
    std::vector<float> nirNorm( static_cast<size_t>( W ) * H );
    std::vector<float> redNorm( static_cast<size_t>( W ) * H );
    std::vector<float> expected( static_cast<size_t>( W ) * H );
    for ( size_t i = 0; i < nirNorm.size(); ++i )
    {
        nirNorm[i] = bands[3][i] * 0.0001f;
        redNorm[i] = bands[2][i] * 0.0001f;
    }
    REQUIRE( SpectralIndices::saviUnit( nirNorm.data(), redNorm.data(),
                                        expected.data(), expected.size() ) );

    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( anchorOutPath, W, H, expected.data(), gt, "EPSG:4326" );

    auto op = RSOperatorRegistry::instance().create( "rs:savi" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = chunkedOutPath.toStdString();
    params["nir"] = 4;
    params["red"] = 3;

    RSOperatorContext ctx;
    Json::Value res = op->run( params, ctx );
    REQUIRE( res["numeric_domain"]["regime"].asString() == "dn_scale" );

    const auto report = sicnu::testing::compareRastersBitExact(
        chunkedOutPath.toStdString(), anchorOutPath.toStdString() );
    if ( !report.identical )
        FAIL( report.detail );
    REQUIRE( report.identical );
}

// ---------------------------------------------------------------------------
// P5: Change Difference & Welford Statistics
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: Change Difference & Welford Statistics",
           "[operators][rs][chunked][parity][change]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 512;
    constexpr int H = 512; // 2x2 tiles of 256x256
    const QString before = tmp.path() + "/before.tif";
    const QString after = tmp.path() + "/after.tif";
    const QString chunkedOut = tmp.path() + "/diff_chunked.tif";
    const QString anchorOut = tmp.path() + "/diff_anchor.tif";

    std::vector<std::vector<float>> bBands( 1 ), aBands( 1 );
    bBands[0].resize( static_cast<size_t>( W ) * H );
    aBands[0].resize( static_cast<size_t>( W ) * H );

    for ( int i = 0; i < W * H; ++i )
    {
        bBands[0][i] = 100.0f + static_cast<float>( i % 37 );
        aBands[0][i] = 110.0f + static_cast<float>( ( i * 3 ) % 41 );
    }
    // Embed NaN in both dates
    bBands[0][50] = std::numeric_limits<float>::quiet_NaN();
    aBands[0][60] = std::numeric_limits<float>::quiet_NaN();

    REQUIRE( writeTestRaster( before, W, H, bBands ).empty() );
    REQUIRE( writeTestRaster( after, W, H, aBands ).empty() );

    // Reference difference & stats
    std::vector<float> expected( static_cast<size_t>( W ) * H );
    for ( size_t i = 0; i < expected.size(); ++i )
    {
        if ( !std::isfinite( bBands[0][i] ) || !std::isfinite( aBands[0][i] ) )
            expected[i] = std::numeric_limits<float>::quiet_NaN();
        else
            expected[i] = aBands[0][i] - bBands[0][i];
    }
    const MathUtils::Stats oracleStats = MathUtils::computeStats( expected.data(), expected.size() );

    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( anchorOut, W, H, expected.data(), gt, "EPSG:4326" );

    auto op = RSOperatorRegistry::instance().create( "rs:change_difference" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["before"] = before.toStdString();
    params["after"] = after.toStdString();
    params["output"] = chunkedOut.toStdString();

    RSOperatorContext ctx;
    Json::Value res = op->run( params, ctx );

    // Raster parity
    const auto report = sicnu::testing::compareRastersBitExact(
        chunkedOut.toStdString(), anchorOut.toStdString() );
    if ( !report.identical )
        FAIL( report.detail );
    REQUIRE( report.identical );

    // Welford statistics equivalence
    REQUIRE( res["mean"].asDouble() == Catch::Approx( oracleStats.mean ).epsilon( 1e-5 ) );
    REQUIRE( res["stddev"].asDouble() == Catch::Approx( oracleStats.stddev ).epsilon( 1e-5 ) );
    REQUIRE( res["total_tiles"].asUInt64() == 4 );
    REQUIRE( res["tiles_computed"].asUInt64() == 4 );
}

// ---------------------------------------------------------------------------
// P6: Crash Resumption Bit-Parity
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: Resumed Run Equals Fresh Run Bit-Exactly",
           "[operators][rs][chunked][parity][resumption]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 600;
    constexpr int H = 600; // 9 tiles of 256x256
    const QString inPath = tmp.path() + "/in_resume.tif";
    const QString freshOut = tmp.path() + "/fresh.tif";
    const QString resumedOut = tmp.path() + "/resumed.tif";
    const std::string stateBase = ( tmp.path() + "/resume_state" ).toStdString();

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );
    for ( int i = 0; i < W * H; ++i )
    {
        bands[2][i] = 20.0f + static_cast<float>( i % 13 ); // Red
        bands[3][i] = 50.0f + static_cast<float>( i % 29 ); // NIR
    }
    REQUIRE( writeTestRaster( inPath, W, H, bands ).empty() );

    auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
    REQUIRE( op != nullptr );

    // 1. Fresh uninterrupted run
    Json::Value paramsFresh( Json::objectValue );
    paramsFresh["input"] = inPath.toStdString();
    paramsFresh["output"] = freshOut.toStdString();
    paramsFresh["nir"] = 4;
    paramsFresh["red"] = 3;

    RSOperatorContext ctxFresh;
    Json::Value resFresh = op->run( paramsFresh, ctxFresh );
    REQUIRE( resFresh["total_tiles"].asUInt64() == 9 );
    REQUIRE( resFresh["tiles_computed"].asUInt64() == 9 );

    // 2. Interrupted run (cancel after 4 tiles)
    Json::Value paramsResumed = paramsFresh;
    paramsResumed["output"] = resumedOut.toStdString();
    paramsResumed["resumeStateBase"] = stateBase;

    std::atomic<int> tileCount{ 0 };
    RSOperatorContext ctxInterrupted;
    ctxInterrupted.setCancelCallback( [&] {
        return ++tileCount > 4;
    } );

    bool threwCancelled = false;
    try
    {
        op->run( paramsResumed, ctxInterrupted );
    }
    catch ( const RSOperatorError &e )
    {
        threwCancelled = ( e.code() == ErrorCode::Cancelled );
    }
    REQUIRE( threwCancelled );

    // 3. Resume run
    RSOperatorContext ctxResume;
    Json::Value resResumed = op->run( paramsResumed, ctxResume );
    REQUIRE( resResumed["total_tiles"].asUInt64() == 9 );
    REQUIRE( resResumed["tiles_reused"].asUInt64() >= 4 );
    REQUIRE( resResumed["tiles_computed"].asUInt64() == 9 - resResumed["tiles_reused"].asUInt64() );

    // 4. Assert bit-exact equality between fresh and resumed outputs
    const auto report = sicnu::testing::compareRastersBitExact(
        freshOut.toStdString(), resumedOut.toStdString() );
    if ( !report.identical )
        FAIL( report.detail );
    REQUIRE( report.identical );
}

// ---------------------------------------------------------------------------
// P7: Pipeline vs Resumable Execution Modes
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: Pipeline vs Resumable Execution Modes",
           "[operators][rs][chunked][parity][modes]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 600;
    constexpr int H = 600;

    // Part A: rs:ndvi
    {
        const QString inPath = tmp.path() + "/in_modes_ndvi.tif";
        const QString resOut = tmp.path() + "/out_ndvi_resumable.tif";
        const QString pipOut = tmp.path() + "/out_ndvi_pipeline.tif";

        std::vector<std::vector<float>> bands( 4 );
        for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );
        for ( int i = 0; i < W * H; ++i )
        {
            bands[2][i] = 20.0f + static_cast<float>( i % 19 );
            bands[3][i] = 60.0f + static_cast<float>( i % 31 );
        }
        REQUIRE( writeTestRaster( inPath, W, H, bands ).empty() );

        auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["input"] = inPath.toStdString();
        params["nir"] = 4;
        params["red"] = 3;

        // Run resumable
        params["output"] = resOut.toStdString();
        params["executionMode"] = "resumable";
        RSOperatorContext ctxRes;
        op->run( params, ctxRes );

        // Run pipeline
        params["output"] = pipOut.toStdString();
        params["executionMode"] = "pipeline";
        RSOperatorContext ctxPip;
        op->run( params, ctxPip );

        const auto report = sicnu::testing::compareRastersBitExact(
            resOut.toStdString(), pipOut.toStdString() );
        if ( !report.identical )
            FAIL( report.detail );
        REQUIRE( report.identical );
    }

    // Part B: rs:change_difference
    {
        const QString before = tmp.path() + "/before_modes.tif";
        const QString after = tmp.path() + "/after_modes.tif";
        const QString resOut = tmp.path() + "/diff_resumable.tif";
        const QString pipOut = tmp.path() + "/diff_pipeline.tif";

        std::vector<std::vector<float>> bBands( 1 ), aBands( 1 );
        bBands[0].resize( static_cast<size_t>( W ) * H );
        aBands[0].resize( static_cast<size_t>( W ) * H );
        for ( int i = 0; i < W * H; ++i )
        {
            bBands[0][i] = 80.0f + static_cast<float>( i % 23 );
            aBands[0][i] = 120.0f + static_cast<float>( i % 29 );
        }
        REQUIRE( writeTestRaster( before, W, H, bBands ).empty() );
        REQUIRE( writeTestRaster( after, W, H, aBands ).empty() );

        auto op = RSOperatorRegistry::instance().create( "rs:change_difference" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();

        // Run resumable
        params["output"] = resOut.toStdString();
        params["executionMode"] = "resumable";
        RSOperatorContext ctxRes;
        op->run( params, ctxRes );

        // Run pipeline
        params["output"] = pipOut.toStdString();
        params["executionMode"] = "pipeline";
        RSOperatorContext ctxPip;
        op->run( params, ctxPip );

        const auto report = sicnu::testing::compareRastersBitExact(
            resOut.toStdString(), pipOut.toStdString() );
        if ( !report.identical )
            FAIL( report.detail );
        REQUIRE( report.identical );
    }
}

// ---------------------------------------------------------------------------
// P8: Edge Tile Clamping on Non-Multiple Dimensions
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: Edge Tile Clamping on Non-Multiple Dimensions",
           "[operators][rs][chunked][parity][edge_clamp]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 317;
    constexpr int H = 289; // Non-multiple of 256; produces 4 tiles (256x256, 61x256, 256x33, 61x33)

    // Part A: rs:ndvi
    {
        const QString inPath = tmp.path() + "/in_edge_ndvi.tif";
        const QString chunkedOut = tmp.path() + "/out_edge_ndvi.tif";
        const QString anchorOut = tmp.path() + "/anchor_edge_ndvi.tif";

        std::vector<std::vector<float>> bands( 4 );
        for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );
        for ( int i = 0; i < W * H; ++i )
        {
            bands[2][i] = 15.0f + static_cast<float>( i % 17 );
            bands[3][i] = 45.0f + static_cast<float>( i % 31 );
        }
        REQUIRE( writeTestRaster( inPath, W, H, bands ).empty() );

        std::vector<float> expected( static_cast<size_t>( W ) * H );
        REQUIRE( MathUtils::normalizedDifference( bands[3].data(), bands[2].data(),
                                                 expected.data(), expected.size() ) );

        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( anchorOut, W, H, expected.data(), gt, "EPSG:4326" );

        auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["input"] = inPath.toStdString();
        params["output"] = chunkedOut.toStdString();
        params["nir"] = 4;
        params["red"] = 3;

        RSOperatorContext ctx;
        Json::Value res = op->run( params, ctx );
        REQUIRE( res["total_tiles"].asUInt64() == 4 );

        const auto report = sicnu::testing::compareRastersBitExact(
            chunkedOut.toStdString(), anchorOut.toStdString() );
        if ( !report.identical )
            FAIL( report.detail );
        REQUIRE( report.identical );
    }

    // Part B: rs:change_difference
    {
        const QString before = tmp.path() + "/before_edge.tif";
        const QString after = tmp.path() + "/after_edge.tif";
        const QString chunkedOut = tmp.path() + "/diff_edge_chunked.tif";
        const QString anchorOut = tmp.path() + "/diff_edge_anchor.tif";

        std::vector<std::vector<float>> bBands( 1 ), aBands( 1 );
        bBands[0].resize( static_cast<size_t>( W ) * H );
        aBands[0].resize( static_cast<size_t>( W ) * H );
        for ( int i = 0; i < W * H; ++i )
        {
            bBands[0][i] = 50.0f + static_cast<float>( i % 13 );
            aBands[0][i] = 90.0f + static_cast<float>( i % 17 );
        }
        REQUIRE( writeTestRaster( before, W, H, bBands ).empty() );
        REQUIRE( writeTestRaster( after, W, H, aBands ).empty() );

        std::vector<float> expected( static_cast<size_t>( W ) * H );
        for ( size_t i = 0; i < expected.size(); ++i )
        {
            expected[i] = aBands[0][i] - bBands[0][i];
        }
        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( anchorOut, W, H, expected.data(), gt, "EPSG:4326" );

        auto op = RSOperatorRegistry::instance().create( "rs:change_difference" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = chunkedOut.toStdString();

        RSOperatorContext ctx;
        Json::Value res = op->run( params, ctx );
        REQUIRE( res["total_tiles"].asUInt64() == 4 );

        const auto report = sicnu::testing::compareRastersBitExact(
            chunkedOut.toStdString(), anchorOut.toStdString() );
        if ( !report.identical )
            FAIL( report.detail );
        REQUIRE( report.identical );
    }
}

// ---------------------------------------------------------------------------
// P9: NoData and Non-Finite NaN Masking
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: NoData and Non-Finite NaN Masking",
           "[operators][rs][chunked][parity][nodata]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 512;
    constexpr int H = 512;

    // Part A: rs:ndvi NoData & Non-Finite Masking
    {
        const QString inPath = tmp.path() + "/in_nodata_ndvi.tif";
        const QString chunkedOut = tmp.path() + "/out_nodata_ndvi.tif";
        const QString anchorOut = tmp.path() + "/anchor_nodata_ndvi.tif";

        std::vector<std::vector<float>> bands( 4 );
        for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );
        for ( int i = 0; i < W * H; ++i )
        {
            bands[2][i] = 20.0f + static_cast<float>( i % 15 );
            bands[3][i] = 40.0f + static_cast<float>( i % 25 );
        }
        // Inject non-finite and sentinel conditions
        bands[2][10] = -9999.0f; // Declared nodata
        bands[3][20] = std::numeric_limits<float>::quiet_NaN();
        bands[2][30] = std::numeric_limits<float>::infinity();
        bands[3][40] = -std::numeric_limits<float>::infinity();
        bands[2][50] = 0.0f; bands[3][50] = 0.0f; // Denominator 0

        REQUIRE( writeTestRaster( inPath, W, H, bands, -9999.0 ).empty() );

        std::vector<float> expected( static_cast<size_t>( W ) * H );
        for ( size_t i = 0; i < expected.size(); ++i )
        {
            const float r = bands[2][i];
            const float n = bands[3][i];
            if ( r == -9999.0f || n == -9999.0f || !std::isfinite( r ) || !std::isfinite( n ) )
            {
                expected[i] = std::numeric_limits<float>::quiet_NaN();
            }
            else
            {
                expected[i] = MathUtils::safeDiv( n - r, n + r );
            }
        }
        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( anchorOut, W, H, expected.data(), gt, "EPSG:4326" );

        auto op = RSOperatorRegistry::instance().create( "rs:ndvi" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["input"] = inPath.toStdString();
        params["output"] = chunkedOut.toStdString();
        params["nir"] = 4;
        params["red"] = 3;

        RSOperatorContext ctx;
        op->run( params, ctx );

        const auto report = sicnu::testing::compareRastersBitExact(
            chunkedOut.toStdString(), anchorOut.toStdString() );
        if ( !report.identical )
            FAIL( report.detail );
        REQUIRE( report.identical );
    }

    // Part B: rs:change_difference NoData & Non-Finite Masking
    {
        const QString before = tmp.path() + "/before_nodata.tif";
        const QString after = tmp.path() + "/after_nodata.tif";
        const QString chunkedOut = tmp.path() + "/diff_nodata_chunked.tif";
        const QString anchorOut = tmp.path() + "/diff_nodata_anchor.tif";

        std::vector<std::vector<float>> bBands( 1 ), aBands( 1 );
        bBands[0].resize( static_cast<size_t>( W ) * H );
        aBands[0].resize( static_cast<size_t>( W ) * H );
        for ( int i = 0; i < W * H; ++i )
        {
            bBands[0][i] = 100.0f + static_cast<float>( i % 19 );
            aBands[0][i] = 120.0f + static_cast<float>( i % 23 );
        }
        bBands[0][10] = -9999.0f;
        aBands[0][20] = std::numeric_limits<float>::quiet_NaN();
        bBands[0][30] = std::numeric_limits<float>::infinity();

        REQUIRE( writeTestRaster( before, W, H, bBands, -9999.0 ).empty() );
        REQUIRE( writeTestRaster( after, W, H, aBands, -9999.0 ).empty() );

        std::vector<float> expected( static_cast<size_t>( W ) * H );
        for ( size_t i = 0; i < expected.size(); ++i )
        {
            const float b = bBands[0][i];
            const float a = aBands[0][i];
            if ( b == -9999.0f || a == -9999.0f || !std::isfinite( b ) || !std::isfinite( a ) )
            {
                expected[i] = std::numeric_limits<float>::quiet_NaN();
            }
            else
            {
                expected[i] = a - b;
            }
        }
        std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
        writeReferenceTiff( anchorOut, W, H, expected.data(), gt, "EPSG:4326" );

        auto op = RSOperatorRegistry::instance().create( "rs:change_difference" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = chunkedOut.toStdString();

        RSOperatorContext ctx;
        op->run( params, ctx );

        const auto report = sicnu::testing::compareRastersBitExact(
            chunkedOut.toStdString(), anchorOut.toStdString() );
        if ( !report.identical )
            FAIL( report.detail );
        REQUIRE( report.identical );
    }
}

// ---------------------------------------------------------------------------
// P10: OutputCommitter Integration
// ---------------------------------------------------------------------------
TEST_CASE( "Chunked Operator Parity: OutputCommitter Integration",
           "[operators][rs][chunked][parity][committer]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 512;
    constexpr int H = 512;
    const QString inPath = tmp.path() + "/in_committer.tif";
    const QString tempOut = tmp.path() + "/temp_spectral.tif";
    const QString stableOut = tmp.path() + "/stable_spectral.tif";
    const QString anchorOut = tmp.path() + "/anchor_spectral.tif";

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );
    for ( int i = 0; i < W * H; ++i )
    {
        bands[2][i] = 20.0f + static_cast<float>( i % 17 ); // Red
        bands[3][i] = 50.0f + static_cast<float>( i % 29 ); // NIR
    }
    REQUIRE( writeTestRaster( inPath, W, H, bands ).empty() );

    std::vector<float> expected( static_cast<size_t>( W ) * H );
    REQUIRE( MathUtils::normalizedDifference( bands[3].data(), bands[2].data(),
                                             expected.data(), expected.size() ) );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    writeReferenceTiff( anchorOut, W, H, expected.data(), gt, "EPSG:4326" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    ChunkedOutputCommitSpec commitSpec;
    commitSpec.committer = &committer;
    commitSpec.tempPath = tempOut;
    commitSpec.stablePath = stableOut;
    commitSpec.kind = sicnu::data::AssetKind::Raster;
    commitSpec.persistence = sicnu::data::PersistencePolicy::SessionTemporary;
    commitSpec.autoLoad = false;

    sicnu::data::DerivationRecord derivation;
    derivation.algorithmId = QStringLiteral( "rs:ndvi" );
    derivation.algorithmVersion = QStringLiteral( "1.0.0" );
    commitSpec.derivation = derivation;

    ChunkedRunOptions opts;
    opts.commitSpec = commitSpec;

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = stableOut.toStdString();
    params["nir"] = 4;
    params["red"] = 3;

    RSOperatorContext ctx;
    Json::Value res = rs::spectral_index_detail::runSpectralIndexCore(
        "NDVI", params, ctx, false, "rs:ndvi", opts );

    // Verify OutputCommitter consumed tempPath and atomically created stablePath
    CHECK_FALSE( QFile::exists( tempOut ) );
    CHECK( QFile::exists( stableOut ) );
    REQUIRE_FALSE( res["published_asset_id"].asString().empty() );

    // Verify bit-exact output
    const auto report = sicnu::testing::compareRastersBitExact(
        stableOut.toStdString(), anchorOut.toStdString() );
    if ( !report.identical )
        FAIL( report.detail );
    REQUIRE( report.identical );
}
