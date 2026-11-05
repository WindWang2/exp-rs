// test_r28_t02_infer_cpu_loop.cpp — R28-T02 WP10: a REAL CPU inference
// closed loop through the production runtime, on the committed deterministic
// ONNX identity fixture (tests/data/test_infer_identity.onnx) via the
// OpenCV DNN provider:
//
//   plan (read-only twin: no error findings)
//     → runModelInference (real cv::dnn forward, tiled engine)
//     → output raster written to disk
//     → REOPENED and per-pixel verified (identity preserves the band)
//     → quality summary (WP11 observability: tiles processed, grid complete).
//
// Compiles ONLY with SICNU_HAS_OPENCV (the rs:infer real-backend gate); on
// hosts without OpenCV the suite is absent and the lane records
// NOT_EXECUTED — this file never fakes a provider.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/inference_plan.h"
#include "operators/runtime/model_execution_service.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <cmath>
#include <string>
#include <vector>

using namespace sicnu::operators::runtime;
using Catch::Approx;

#ifdef SICNU_HAS_OPENCV
namespace
{
constexpr int kWidth = 24;
constexpr int kHeight = 18;

/// Writes a single-band Float32 raster with distinct per-pixel values
/// (row-major counter): identity must give each back within float tolerance.
std::string writeInputRaster( const QTemporaryDir &dir )
{
    const std::string path = ( QDir( dir.path() ).filePath( QStringLiteral( "in.tif" ) ) ).toStdString();
    GDALAllRegister(); // this TU creates datasets directly; readers register lazily
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.c_str(), kWidth, kHeight, 1, GDT_Float32,
                                  nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> pixels( static_cast<std::size_t>( kWidth ) * kHeight );
    for ( int y = 0; y < kHeight; ++y )
        for ( int x = 0; x < kWidth; ++x )
            pixels[static_cast<std::size_t>( y ) * kWidth + x] =
                static_cast<float>( ( y * kWidth + x ) % 251 ) * 0.5f;
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, kWidth, kHeight, pixels.data(), kWidth,
                           kHeight, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
    return path;
}
} // namespace

TEST_CASE( "real CPU inference closed loop: plan → infer → write → reopen → quality",
           "[r28t02][infer][opencv]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string input = writeInputRaster( dir );
    const std::string model = ( QDir( QString::fromStdString( TESTS_DATA_DIR ) )
                                    .filePath( QStringLiteral( "test_infer_identity.onnx" ) ) )
                                  .toStdString();
    REQUIRE( QFileInfo::exists( QString::fromStdString( model ) ) );
    const std::string output =
        ( QDir( dir.path() ).filePath( QStringLiteral( "out.tif" ) ) ).toStdString();

    ModelExecutionRequest request;
    request.inputPath = input;
    request.modelReference = model;
    request.outputPath = output;

    // 1) The read-only plan twin: a resolvable model + readable input + a
    // CPU-capable runtime must produce ZERO "error" findings (every typed
    // refusal the run could raise would appear here first).
    const InferencePlan plan = planModelInference( request );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Ready );
    int errorFindings = 0;
    for ( const InferencePlanFinding &finding : plan.findings )
        if ( finding.severity == "error" )
            ++errorFindings;
    INFO( "plan error findings: " << errorFindings );
    REQUIRE( errorFindings == 0 );
    REQUIRE( plan.tilesPlanned >= 1 );
    REQUIRE( plan.batchWorkingSetMiB > 0.0 ); // the WP09 estimate is populated

    // 2) Real execution through the OpenCV DNN provider.
    sicnu::operators::RSOperatorContext context;
    const ModelExecutionResult result = runModelInference( request, context );
    REQUIRE( result.payload.isObject() );
    CHECK( result.payload["backend"].asString() == "opencv_dnn" );
    // WP11 observability: the payload names the device that actually ran.
    CHECK( !result.payload["device"].asString().empty() );
    CHECK( result.payload["tiles"].asInt() >= 1 );

    // 3) The artifact exists and REOPENS with the input geometry.
    REQUIRE( QFileInfo::exists( QString::fromStdString( output ) ) );
    GDALDatasetH out = GDALOpen( output.c_str(), GA_ReadOnly );
    REQUIRE( out != nullptr );
    CHECK( GDALGetRasterXSize( out ) == kWidth );
    CHECK( GDALGetRasterYSize( out ) == kHeight );
    GDALRasterBandH band = GDALGetRasterBand( out, 1 );
    std::vector<float> pixels( static_cast<std::size_t>( kWidth ) * kHeight );
    REQUIRE( GDALRasterIO( band, GF_Read, 0, 0, kWidth, kHeight, pixels.data(), kWidth,
                           kHeight, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( out );
    for ( int y = 0; y < kHeight; ++y )
        for ( int x = 0; x < kWidth; ++x )
        {
            const float expected =
                static_cast<float>( ( y * kWidth + x ) % 251 ) * 0.5f;
            CHECK( pixels[static_cast<std::size_t>( y ) * kWidth + x]
                   == Approx( expected ).margin( 1e-3 ) );
        }

    // 4) Quality summary (WP11): the typed projection of the finished run.
    const InferenceQualitySummary quality = summarizeInferenceQuality( result );
    CHECK( quality.tilesProcessed >= 1 );
    // Identity coverage: every planned tile ran (a grid hole would surface
    // here as processed < planned).
    CHECK( quality.tilesProcessed >= quality.tilesPlanned );
}
#endif // SICNU_HAS_OPENCV
