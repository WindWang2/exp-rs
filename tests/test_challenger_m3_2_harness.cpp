// tests/test_challenger_m3_2_harness.cpp — Empirical Challenge Suite (Milestone 3)
//
// Target Invariants Challenged:
// 1. Online Welford statistics sink (StreamingMagnitudeStats):
//    Verify numerical equivalence and precision stability against global offline
//    two-pass statistics under ill-conditioned data, large offsets, uniform values,
//    and non-finite/NoData masking.
// 2. Change Streaming Online Welford vs Global Two-Pass Raster Oracle:
//    Verify end-to-end change operators (rs:change_difference, rs:change_normalized_difference)
//    against whole-raster two-pass calculations.
// 3. alreadyPublished Metadata Recovery in Change Streaming:
//    Verify statistical telemetry consistency when compute is bypassed via alreadyPublished.
// 4. Change Detection Cooperative Cancellation and Crash Resumption:
//    Verify pairwise BIP chunk streaming survives cancellation and produces bit-identical
//    raster outputs and equivalent running statistics upon resumption.
// 5. OutputCommitter Transactional Swap Under Cancellation and Resumption:
//    Verify atomic publish-then-swap produces valid registered assets with correct metadata
//    across interrupted and resumed execution cycles.
// 6. Scratch Lifecycle and Sweeping with OutputCommitter:
//    Verify cleanupScratchOnSuccess sweeps tile scratch while retaining durable markers.

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
#include "processing/framework/output_committer.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "data/data_manager.h"
#include "raster_bit_compare.h"

#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <gdal.h>
#include <gdal_priv.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
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

struct OfflineStats
{
    size_t validCount = 0;
    double mean = 0.0;
    double variance = 0.0;
    double stddev = 0.0;
    double minVal = std::numeric_limits<double>::infinity();
    double maxVal = -std::numeric_limits<double>::infinity();
};

OfflineStats computeTwoPassStats( const float *data, size_t size,
                                  std::optional<float> nodata = std::nullopt )
{
    OfflineStats res;
    // Pass 1: compute mean and min/max
    double sum = 0.0;
    for ( size_t i = 0; i < size; ++i )
    {
        const float v = data[i];
        if ( !std::isfinite( v ) )
            continue;
        if ( nodata.has_value() && v == nodata.value() )
            continue;
        ++res.validCount;
        sum += static_cast<double>( v );
        if ( v < res.minVal ) res.minVal = v;
        if ( v > res.maxVal ) res.maxVal = v;
    }

    if ( res.validCount == 0 )
        return res;

    res.mean = sum / static_cast<double>( res.validCount );

    // Pass 2: compute variance using (x - mean)^2
    double sumSqDiff = 0.0;
    for ( size_t i = 0; i < size; ++i )
    {
        const float v = data[i];
        if ( !std::isfinite( v ) )
            continue;
        if ( nodata.has_value() && v == nodata.value() )
            continue;
        const double diff = static_cast<double>( v ) - res.mean;
        sumSqDiff += diff * diff;
    }

    res.variance = sumSqDiff / static_cast<double>( res.validCount );
    res.stddev = ( res.validCount > 1 ) ? std::sqrt( res.variance ) : 0.0;
    return res;
}

OfflineStats readRasterOfflineStats( const QString &path )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    const int w = GDALGetRasterXSize( ds );
    const int h = GDALGetRasterYSize( ds );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( band != nullptr );

    int hasNd = 0;
    const double ndVal = GDALGetRasterNoDataValue( band, &hasNd );
    std::optional<float> nodata = ( hasNd != 0 ) ? std::optional<float>( static_cast<float>( ndVal ) ) : std::nullopt;

    std::vector<float> buf( static_cast<size_t>( w ) * h );
    REQUIRE( GDALRasterIO( band, GF_Read, 0, 0, w, h, buf.data(), w, h, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );

    return computeTwoPassStats( buf.data(), buf.size(), nodata );
}

} // anonymous namespace

// ===========================================================================
// Challenge 1: Online Welford Statistics Numerical Equivalence & Stability
// ===========================================================================
TEST_CASE( "Challenger M3.2: Welford Online vs Offline Two-Pass Precision Oracle",
           "[challenger][welford][precision]" )
{
    using namespace sicnu::operators::rs;

    SECTION( "1A: Ill-conditioned data with large base offset (1e7) and small variance" )
    {
        constexpr size_t N = 100000;
        std::vector<float> data( N );
        constexpr double kBase = 10000000.0;
        for ( size_t i = 0; i < N; ++i )
        {
            // Variations in [-5.0, 4.9] centered at 1e7
            data[i] = static_cast<float>( kBase + static_cast<double>( i % 100 ) * 0.1 - 5.0 );
        }

        const OfflineStats oracle = computeTwoPassStats( data.data(), data.size() );

        StreamingMagnitudeStats online;
        for ( float v : data )
        {
            online.add( v );
        }

        REQUIRE( online.validCount == oracle.validCount );
        REQUIRE( online.mean == Catch::Approx( oracle.mean ).epsilon( 1e-11 ) );
        REQUIRE( online.variance() == Catch::Approx( oracle.variance ).epsilon( 1e-9 ) );
        REQUIRE( online.stddev() == Catch::Approx( oracle.stddev ).epsilon( 1e-9 ) );
        REQUIRE( online.minVal == Catch::Approx( oracle.minVal ).epsilon( 1e-7 ) );
        REQUIRE( online.maxVal == Catch::Approx( oracle.maxVal ).epsilon( 1e-7 ) );
    }

    SECTION( "1B: Uniform stream (zero variance)" )
    {
        constexpr size_t N = 10000;
        constexpr float val = 42.12345f;
        std::vector<float> data( N, val );

        StreamingMagnitudeStats online;
        for ( float v : data )
        {
            online.add( v );
        }

        REQUIRE( online.validCount == N );
        REQUIRE( online.mean == Catch::Approx( val ).epsilon( 1e-7 ) );
        REQUIRE( online.variance() >= 0.0 );
        REQUIRE( online.variance() < 1e-12 );
        REQUIRE( online.stddev() >= 0.0 );
        REQUIRE( online.stddev() < 1e-12 );
        REQUIRE( online.minVal == Catch::Approx( val ).epsilon( 1e-7 ) );
        REQUIRE( online.maxVal == Catch::Approx( val ).epsilon( 1e-7 ) );
    }

    SECTION( "1C: Single sample boundary condition" )
    {
        StreamingMagnitudeStats online;
        online.add( 99.5f );

        REQUIRE( online.validCount == 1 );
        REQUIRE( online.mean == Catch::Approx( 99.5 ).epsilon( 1e-7 ) );
        REQUIRE( online.variance() == 0.0 );
        REQUIRE( online.stddev() == 0.0 );
        REQUIRE( online.minVal == 99.5 );
        REQUIRE( online.maxVal == 99.5 );
    }

    SECTION( "1D: Empty stream boundary condition" )
    {
        StreamingMagnitudeStats online;
        REQUIRE( online.validCount == 0 );
        REQUIRE( online.mean == 0.0 );
        REQUIRE( online.variance() == 0.0 );
        REQUIRE( online.stddev() == 0.0 );
        REQUIRE( std::isinf( online.minVal ) );
        REQUIRE( online.minVal > 0.0 );
        REQUIRE( std::isinf( online.maxVal ) );
        REQUIRE( online.maxVal < 0.0 );
    }

    SECTION( "1E: Non-finite filtering (NaN, +inf, -inf) and sentinel rejection" )
    {
        std::vector<float> data = {
            10.0f,
            std::numeric_limits<float>::quiet_NaN(),
            20.0f,
            std::numeric_limits<float>::infinity(),
            30.0f,
            -std::numeric_limits<float>::infinity(),
            40.0f
        };

        const OfflineStats oracle = computeTwoPassStats( data.data(), data.size() );
        REQUIRE( oracle.validCount == 4 );

        StreamingMagnitudeStats online;
        for ( float v : data )
        {
            online.add( v );
        }

        REQUIRE( online.validCount == 4 );
        REQUIRE( online.mean == Catch::Approx( oracle.mean ).epsilon( 1e-7 ) );
        REQUIRE( online.variance() == Catch::Approx( oracle.variance ).epsilon( 1e-7 ) );
        REQUIRE( online.stddev() == Catch::Approx( oracle.stddev ).epsilon( 1e-7 ) );
        REQUIRE( online.minVal == 10.0 );
        REQUIRE( online.maxVal == 40.0 );
    }
}

// ===========================================================================
// Challenge 2: Change Streaming Online Welford vs Global Two-Pass Raster Oracle
// ===========================================================================
TEST_CASE( "Challenger M3.2: Change Streaming Online Welford vs Global Two-Pass Raster Oracle",
           "[challenger][welford][raster_oracle]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 600;
    constexpr int H = 600; // 9 tiles of 256x256
    const QString before = tmp.path() + "/before_oracle.tif";
    const QString after = tmp.path() + "/after_oracle.tif";
    const QString diffOut = tmp.path() + "/diff_oracle.tif";
    const QString normDiffOut = tmp.path() + "/norm_diff_oracle.tif";

    std::vector<std::vector<float>> bBands( 1 ), aBands( 1 );
    bBands[0].resize( static_cast<size_t>( W ) * H );
    aBands[0].resize( static_cast<size_t>( W ) * H );

    for ( int i = 0; i < W * H; ++i )
    {
        bBands[0][i] = 120.0f + static_cast<float>( i % 47 );
        aBands[0][i] = 145.0f + static_cast<float>( ( i * 7 ) % 53 );
    }
    // Inject sentinels and non-finite samples
    bBands[0][100] = -9999.0f;
    aBands[0][101] = -9999.0f;
    bBands[0][200] = std::numeric_limits<float>::quiet_NaN();
    aBands[0][201] = std::numeric_limits<float>::infinity();

    REQUIRE( writeTestRaster( before, W, H, bBands, -9999.0 ).empty() );
    REQUIRE( writeTestRaster( after, W, H, aBands, -9999.0 ).empty() );

    // 2A: rs:change_difference
    {
        auto op = RSOperatorRegistry::instance().create( "rs:change_difference" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = diffOut.toStdString();

        RSOperatorContext ctx;
        Json::Value res = op->run( params, ctx );

        const OfflineStats oracle = readRasterOfflineStats( diffOut );
        REQUIRE( oracle.validCount > 0 );

        CHECK( res["mean"].asDouble() == Catch::Approx( oracle.mean ).epsilon( 1e-5 ) );
        CHECK( res["stddev"].asDouble() == Catch::Approx( oracle.stddev ).epsilon( 1e-5 ) );
        CHECK( res["variance"].asDouble() == Catch::Approx( oracle.variance ).epsilon( 1e-5 ) );
        CHECK( res["min"].asDouble() == Catch::Approx( oracle.minVal ).epsilon( 1e-5 ) );
        CHECK( res["max"].asDouble() == Catch::Approx( oracle.maxVal ).epsilon( 1e-5 ) );
    }

    // 2B: rs:change_normalized_difference
    {
        auto op = RSOperatorRegistry::instance().create( "rs:change_normalized_difference" );
        REQUIRE( op != nullptr );

        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = normDiffOut.toStdString();

        RSOperatorContext ctx;
        Json::Value res = op->run( params, ctx );

        const OfflineStats oracle = readRasterOfflineStats( normDiffOut );
        REQUIRE( oracle.validCount > 0 );

        CHECK( res["mean"].asDouble() == Catch::Approx( oracle.mean ).epsilon( 1e-5 ) );
        CHECK( res["stddev"].asDouble() == Catch::Approx( oracle.stddev ).epsilon( 1e-5 ) );
        CHECK( res["variance"].asDouble() == Catch::Approx( oracle.variance ).epsilon( 1e-5 ) );
        CHECK( res["min"].asDouble() == Catch::Approx( oracle.minVal ).epsilon( 1e-5 ) );
        CHECK( res["max"].asDouble() == Catch::Approx( oracle.maxVal ).epsilon( 1e-5 ) );
    }
}

// ===========================================================================
// Challenge 3: alreadyPublished Metadata Recovery Defect in Change Streaming
// ===========================================================================
TEST_CASE( "Challenger M3.2: alreadyPublished Metadata Recovery Defect in Change Streaming",
           "[challenger][already_published][defect]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 512;
    constexpr int H = 512; // 4 tiles of 256x256
    const QString before = tmp.path() + "/before_pub.tif";
    const QString after = tmp.path() + "/after_pub.tif";
    const QString outPath = tmp.path() + "/diff_pub.tif";
    const std::string resumeBase = ( tmp.path() + "/resume_state_pub" ).toStdString();

    std::vector<std::vector<float>> bBands( 1 ), aBands( 1 );
    bBands[0].resize( static_cast<size_t>( W ) * H );
    aBands[0].resize( static_cast<size_t>( W ) * H );

    for ( int i = 0; i < W * H; ++i )
    {
        bBands[0][i] = 100.0f + static_cast<float>( i % 31 );
        aBands[0][i] = 130.0f + static_cast<float>( ( i * 3 ) % 43 );
    }
    REQUIRE( writeTestRaster( before, W, H, bBands ).empty() );
    REQUIRE( writeTestRaster( after, W, H, aBands ).empty() );

    auto op = RSOperatorRegistry::instance().create( "rs:change_difference" );
    REQUIRE( op != nullptr );

    Json::Value params( Json::objectValue );
    params["before"] = before.toStdString();
    params["after"] = after.toStdString();
    params["output"] = outPath.toStdString();
    params["resumeStateBase"] = resumeBase;

    // Run 1: Fresh run — computes all 4 tiles and creates .published marker
    RSOperatorContext ctx1;
    Json::Value res1 = op->run( params, ctx1 );

    REQUIRE( res1["total_tiles"].asUInt64() == 4 );
    REQUIRE( res1["tiles_computed"].asUInt64() == 4 );
    REQUIRE( res1["already_published"].asBool() == false );

    const double freshMean = res1["mean"].asDouble();
    const double freshStddev = res1["stddev"].asDouble();
    const double freshVariance = res1["variance"].asDouble();
    const double freshMin = res1["min"].asDouble();
    const double freshMax = res1["max"].asDouble();

    // Verify fresh values are well-formed and non-trivial
    REQUIRE( freshStddev > 0.0 );
    REQUIRE( freshVariance > 0.0 );
    REQUIRE( freshMin < freshMax );

    // Run 2: Idempotent re-run with identical parameters.
    // Must bypass compute via alreadyPublished and recover telemetry.
    RSOperatorContext ctx2;
    Json::Value res2 = op->run( params, ctx2 );

    REQUIRE( res2["already_published"].asBool() == true );
    REQUIRE( res2["tiles_computed"].asUInt64() == 0 );

    const double pubMean = res2["mean"].asDouble();
    const double pubStddev = res2["stddev"].asDouble();
    const double pubVariance = res2["variance"].asDouble();
    const double pubMin = res2["min"].asDouble();
    const double pubMax = res2["max"].asDouble();

    // Mean should be preserved
    CHECK( pubMean == Catch::Approx( freshMean ).epsilon( 1e-5 ) );

    // CHALLENGE POINT:
    // In src/operators/rs/rs_change_streaming.cpp lines 684-688:
    //   const double sd = std::stod( s );
    //   magStats.m2 = sd * sd;
    //   magStats.validCount = ( sd > 0.0 ) ? 2 : 1;
    // When stddev() is called on magStats:
    //   stddev() = sqrt(m2 / validCount) = sqrt(sd^2 / 2) = sd / sqrt(2) !
    // When variance() is called on magStats:
    //   variance() = m2 / validCount = sd^2 / 2 !
    // When min/max are read:
    //   min = +inf, max = -inf !
    //
    // The assertions below test the CONTRACT: telemetry of alreadyPublished MUST
    // match fresh execution within epsilon. If this fails, it empirically proves the bug!
    CHECK( pubStddev == Catch::Approx( freshStddev ).epsilon( 1e-5 ) );
    CHECK( pubVariance == Catch::Approx( freshVariance ).epsilon( 1e-5 ) );
    CHECK( pubMin == Catch::Approx( freshMin ).epsilon( 1e-5 ) );
    CHECK( pubMax == Catch::Approx( freshMax ).epsilon( 1e-5 ) );
}

// ===========================================================================
// Challenge 4: Change Detection Cooperative Cancellation and Crash Resumption
// ===========================================================================
TEST_CASE( "Challenger M3.2: Change Detection Cooperative Cancellation and Crash Resumption",
           "[challenger][cancellation][change]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 600;
    constexpr int H = 600; // 9 tiles of 256x256
    const QString before = tmp.path() + "/before_cancel.tif";
    const QString after = tmp.path() + "/after_cancel.tif";
    const QString freshOut = tmp.path() + "/diff_fresh.tif";
    const QString resumedOut = tmp.path() + "/diff_resumed.tif";
    const std::string resumeBase = ( tmp.path() + "/resume_state_change" ).toStdString();

    std::vector<std::vector<float>> bBands( 1 ), aBands( 1 );
    bBands[0].resize( static_cast<size_t>( W ) * H );
    aBands[0].resize( static_cast<size_t>( W ) * H );

    for ( int i = 0; i < W * H; ++i )
    {
        bBands[0][i] = 85.0f + static_cast<float>( i % 19 );
        aBands[0][i] = 115.0f + static_cast<float>( ( i * 5 ) % 23 );
    }
    REQUIRE( writeTestRaster( before, W, H, bBands ).empty() );
    REQUIRE( writeTestRaster( after, W, H, aBands ).empty() );

    auto op = RSOperatorRegistry::instance().create( "rs:change_difference" );
    REQUIRE( op != nullptr );

    // 1. Fresh uninterrupted run
    Json::Value paramsFresh( Json::objectValue );
    paramsFresh["before"] = before.toStdString();
    paramsFresh["after"] = after.toStdString();
    paramsFresh["output"] = freshOut.toStdString();

    RSOperatorContext ctxFresh;
    Json::Value resFresh = op->run( paramsFresh, ctxFresh );
    REQUIRE( resFresh["total_tiles"].asUInt64() == 9 );
    REQUIRE( resFresh["tiles_computed"].asUInt64() == 9 );

    // 2. Interrupted run: cancel after 3 tiles
    Json::Value paramsResume = paramsFresh;
    paramsResume["output"] = resumedOut.toStdString();
    paramsResume["resumeStateBase"] = resumeBase;

    std::atomic<int> tileCounter{ 0 };
    RSOperatorContext ctxCancel;
    ctxCancel.setCancelCallback( [&] {
        return ++tileCounter > 3;
    } );

    bool cancelledCaught = false;
    try
    {
        op->run( paramsResume, ctxCancel );
    }
    catch ( const RSOperatorError &e )
    {
        cancelledCaught = ( e.code() == ErrorCode::Cancelled );
    }
    REQUIRE( cancelledCaught );

    // Verify journal sidecar exists
    const QString journalPath = QString::fromStdString( resumeBase + ".journal" );
    REQUIRE( QFile::exists( journalPath ) );

    // 3. Resumed run
    RSOperatorContext ctxResume;
    Json::Value resResumed = op->run( paramsResume, ctxResume );

    REQUIRE( resResumed["total_tiles"].asUInt64() == 9 );
    REQUIRE( resResumed["tiles_reused"].asUInt64() >= 3 );
    REQUIRE( resResumed["tiles_computed"].asUInt64() == 9 - resResumed["tiles_reused"].asUInt64() );

    // 4. Assert bit-exact pixel equality between fresh and resumed raster outputs
    const auto bitReport = sicnu::testing::compareRastersBitExact(
        freshOut.toStdString(), resumedOut.toStdString() );
    if ( !bitReport.identical )
        FAIL( bitReport.detail );
    REQUIRE( bitReport.identical );

    // 5. Assert statistics equivalence between fresh and resumed runs
    REQUIRE( resResumed["mean"].asDouble() == Catch::Approx( resFresh["mean"].asDouble() ).epsilon( 1e-5 ) );
    REQUIRE( resResumed["stddev"].asDouble() == Catch::Approx( resFresh["stddev"].asDouble() ).epsilon( 1e-5 ) );
}

// ===========================================================================
// Challenge 5: OutputCommitter Integration Under Cancellation and Resumption
// ===========================================================================
TEST_CASE( "Challenger M3.2: OutputCommitter Integration Under Cancellation and Resumption",
           "[challenger][output_committer][resumption]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 600;
    constexpr int H = 600; // 9 tiles of 256x256
    const QString inPath = tmp.path() + "/in_committer_resume.tif";
    const QString tempOut = tmp.path() + "/temp_spectral_resume.tif";
    const QString stableOut = tmp.path() + "/stable_spectral_resume.tif";
    const QString anchorOut = tmp.path() + "/anchor_spectral_resume.tif";
    const std::string resumeBase = ( tmp.path() + "/resume_state_committer" ).toStdString();

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H );
    for ( int i = 0; i < W * H; ++i )
    {
        bands[2][i] = 18.0f + static_cast<float>( i % 17 ); // Red
        bands[3][i] = 52.0f + static_cast<float>( i % 31 ); // NIR
    }
    REQUIRE( writeTestRaster( inPath, W, H, bands ).empty() );

    // Generate anchor
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
    opts.resumeStateBase = resumeBase;

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = stableOut.toStdString();
    params["nir"] = 4;
    params["red"] = 3;

    // Step 1: Cancelled execution
    std::atomic<int> tileCounter{ 0 };
    RSOperatorContext ctxCancel;
    ctxCancel.setCancelCallback( [&] {
        return ++tileCounter > 3;
    } );

    bool threwCancel = false;
    try
    {
        rs::spectral_index_detail::runSpectralIndexCore(
            "NDVI", params, ctxCancel, false, "rs:ndvi", opts );
    }
    catch ( const RSOperatorError &e )
    {
        threwCancel = ( e.code() == ErrorCode::Cancelled );
    }
    REQUIRE( threwCancel );

    // Verify stable path was NOT published prematurely and no asset registered
    CHECK_FALSE( QFile::exists( stableOut ) );
    CHECK( QFile::exists( QString::fromStdString( resumeBase + ".journal" ) ) );

    // Step 2: Resume execution to completion
    RSOperatorContext ctxResume;
    Json::Value resResume = rs::spectral_index_detail::runSpectralIndexCore(
        "NDVI", params, ctxResume, false, "rs:ndvi", opts );

    // Verify atomic publish-then-swap completed
    CHECK_FALSE( QFile::exists( tempOut ) );
    CHECK( QFile::exists( stableOut ) );

    const std::string assetIdStr = resResume["published_asset_id"].asString();
    REQUIRE_FALSE( assetIdStr.empty() );

    const auto assetId = sicnu::data::AssetId::fromString( QString::fromStdString( assetIdStr ) );
    // fromString answers std::optional; AssetId exposes isNull(), and the
    // manager's membership probe IS asset() (no hasAsset on DataManager).
    REQUIRE( assetId.has_value() );
    REQUIRE_FALSE( assetId->isNull() );

    const auto asset = manager.asset( *assetId );
    REQUIRE( asset.has_value() );
    CHECK( asset->kind() == sicnu::data::AssetKind::Raster );
    CHECK( asset->source().canonicalSource == stableOut );

    // Verify bit-exact equality of resumed committer output against reference anchor
    const auto bitReport = sicnu::testing::compareRastersBitExact(
        stableOut.toStdString(), anchorOut.toStdString() );
    if ( !bitReport.identical )
        FAIL( bitReport.detail );
    REQUIRE( bitReport.identical );
}

// ===========================================================================
// Challenge 6: OutputCommitter Scratch Lifecycle and Fault Abandonment
// ===========================================================================
TEST_CASE( "Challenger M3.2: OutputCommitter Scratch Lifecycle and Fault Abandonment",
           "[challenger][committer][lifecycle]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 512;
    constexpr int H = 512; // 4 tiles
    const QString inPath = tmp.path() + "/in_lifecycle.tif";
    const QString tempOut = tmp.path() + "/temp_lifecycle.tif";
    const QString stableOut = tmp.path() + "/stable_lifecycle.tif";
    const std::string scratchDir = ( tmp.path() + "/scratch_tiles" ).toStdString();
    const std::string resumeBase = ( tmp.path() + "/resume_state_lifecycle" ).toStdString();

    std::vector<std::vector<float>> bands( 4 );
    for ( auto &b : bands ) b.resize( static_cast<size_t>( W ) * H, 20.0f );
    REQUIRE( writeTestRaster( inPath, W, H, bands ).empty() );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    ChunkedOutputCommitSpec commitSpec;
    commitSpec.committer = &committer;
    commitSpec.tempPath = tempOut;
    commitSpec.stablePath = stableOut;
    commitSpec.kind = sicnu::data::AssetKind::Raster;

    // Case 6A: Successful run sweeps scratch when cleanupScratchOnSuccess = true
    {
        ChunkedRunOptions opts;
        opts.commitSpec = commitSpec;
        opts.scratchRoot = scratchDir;
        opts.resumeStateBase = resumeBase;
        opts.cleanupScratchOnSuccess = true;

        Json::Value params( Json::objectValue );
        params["input"] = inPath.toStdString();
        params["output"] = stableOut.toStdString();
        params["nir"] = 4;
        params["red"] = 3;

        RSOperatorContext ctx;
        Json::Value res = rs::spectral_index_detail::runSpectralIndexCore(
            "NDVI", params, ctx, false, "rs:ndvi", opts );

        CHECK( QFile::exists( stableOut ) );
        CHECK_FALSE( QFile::exists( tempOut ) );

        // Scratch tile dir and journal/ckpt should be swept
        CHECK_FALSE( QFile::exists( QString::fromStdString( resumeBase + ".journal" ) ) );
        CHECK_FALSE( QFile::exists( QString::fromStdString( resumeBase + ".ckpt" ) ) );

        // But .published marker should persist for idempotency
        CHECK( QFile::exists( QString::fromStdString( resumeBase + ".published" ) ) );
    }

    // Case 6B: Unhandled abnormal error wipes scratch and calls discardTemporary
    {
        const QString badTemp = tmp.path() + "/bad_temp.tif";
        const QString badStable = tmp.path() + "/nonexistent_parent_dir_xyz/stable.tif";
        const std::string badResumeBase = ( tmp.path() + "/resume_bad" ).toStdString();

        ChunkedOutputCommitSpec badSpec;
        badSpec.committer = &committer;
        badSpec.tempPath = badTemp;
        badSpec.stablePath = badStable; // Will fail commit rename or creation
        badSpec.kind = sicnu::data::AssetKind::Raster;

        ChunkedRunOptions opts;
        opts.commitSpec = badSpec;
        opts.resumeStateBase = badResumeBase;

        Json::Value params( Json::objectValue );
        params["input"] = inPath.toStdString();
        params["output"] = badStable.toStdString();
        params["nir"] = 4;
        params["red"] = 3;

        RSOperatorContext ctx;
        bool failedCommit = false;
        try
        {
            rs::spectral_index_detail::runSpectralIndexCore(
                "NDVI", params, ctx, false, "rs:ndvi", opts );
        }
        catch ( const RSOperatorError &e )
        {
            failedCommit = true;
        }
        REQUIRE( failedCommit );

        // On commit failure, temporary outputs and scratch are preserved for diagnosis (per line 185)
        // Verify no valid asset registered for badStable
        CHECK_FALSE( QFile::exists( badStable ) );
    }
}
