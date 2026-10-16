// tests/test_r15_C_provider_degradation.cpp — Track 15 WP-C05/C14/C16: the
// provider layer's user-facing contracts.
//
// Oracles (.planning/r15-c/ORACLES.md):
//   C16 (B2): a framework whose provider is absent from this build reports
//     the provider's OWN recovery text ("rebuild with ONNX Runtime", "use
//     the onnx framework here", …) on top of the generic unavailability
//     verdict — the actionable text used to be dead code.
//   C05 (B4): one user mistake (a fed tensor that does not fit the graph)
//     classifies to the SAME failure kind and the SAME ErrorCode on every
//     provider spelling (opencv_dnn / TensorRT / ONNX Runtime / OpenVINO).
//   C05 (B3): a session that supports NEITHER multi-input seam refuses a
//     multi-input manifest BEFORE the first forward — never after the input
//     rasters were opened and read.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tensorrt_provider.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>

#include <opencv2/core.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <memory>
#include <string>

namespace {

using namespace sicnu::operators;
using namespace sicnu::operators::runtime;
using sicnu::operators::runtime::classifyInferenceError;
using sicnu::operators::runtime::errorCodeForInferenceFailure;
using sicnu::operators::runtime::InferenceFailureKind;
using sicnu::operators::runtime::ModelRuntimeRegistry;

/// A session that declares NEITHER multi-input seam (the historical default).
class SingleSeamRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "r15c-single"; }
    std::string backendName() const override { return "r15c-single"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r15c-single"; }
    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }
};

} // namespace

TEST_CASE( "R15-C C16: an absent provider's recovery text reaches the readiness verdict",
           "[models][runtime][r15-c]" )
{
    auto &registry = ModelRuntimeRegistry::instance();
    ModelInfo model;
    model.name = "r15c-trt";
    model.framework = "tensorrt";
    model.runtime.device = "cpu";

    const ModelHardwareCapabilities hw; // CPU-only host view
    std::string reason;
    const ModelReadiness verdict = evaluateRuntimeReadiness( model, hw, &reason );

    // TensorRT is not compiled into a default build; when it IS present this
    // model is Ready and the test degrades to the availability assertion.
    if ( registry.hasProvider( "tensorrt" ) )
    {
        CHECK( verdict == ModelReadiness::Ready );
    }
    else
    {
        CHECK( verdict == ModelReadiness::UnsupportedRuntime );
        CHECK_THAT( reason, Catch::Matchers::ContainsSubstring( "no runtime provider available" ) );
        // THE ORACLE: the generic sentence is composed with the provider's
        // own recovery path (previously dead code — the user got only the
        // generic sentence, with no hint about what to install or which
        // framework to switch to).
        const std::string hint = tensorRTUnavailableReason();
        CHECK_FALSE( hint.empty() );
        CHECK_THAT( reason, Catch::Matchers::ContainsSubstring( hint ) );
        // The acquire path carries the same composition.
        std::string acquireError;
        auto session = registry.acquire( model, &acquireError );
        CHECK( session == nullptr );
        CHECK_THAT( acquireError, Catch::Matchers::ContainsSubstring( hint ) );
    }
}

TEST_CASE( "R15-C C05: one fed-tensor mistake classifies identically on every provider spelling",
           "[models][runtime][r15-c]" )
{
    // The same user error as each provider spells it.
    struct Case
    {
        const char *what;
        InferenceFailureKind kind;
    };
    const Case cases[] = {
        // opencv_dnn: rank refusal and dtype refusal.
        { "inference input must be a 4-D NCHW blob", InferenceFailureKind::ShapeMismatch },
        { "inference input must be float32 (capabilities() declares input_dtypes float32 "
          "only), got cv depth 0",
          InferenceFailureKind::IncompatibleSchema },
        // TensorRT: the binding-size refusal (used to fall through to Unknown).
        { "fed blob size does not match the TensorRT engine input binding (engine expects 12 "
          "elements)",
          InferenceFailureKind::ShapeMismatch },        // ONNX Runtime: the rank-ceiling refusal (used to fall through to Unknown).
        { "input 'x' rank 7 exceeds this provider's maximum rank (6)",
          InferenceFailureKind::ShapeMismatch },
        { "input blobs must be 4-D CV_32F NCHW ('before')", InferenceFailureKind::ShapeMismatch },
        // OpenVINO: the shape refusal (already matched).
        { "fed blob size does not match the OpenVINO input shape (12 elements expected)",
          InferenceFailureKind::ShapeMismatch },
    };
    for ( const Case &c : cases )
    {
        INFO( c.what );
        CHECK( classifyInferenceError( c.what ) == c.kind );
        // And the ErrorCode projection agrees for every spelling.
        CHECK( errorCodeForInferenceFailure( classifyInferenceError( c.what ) )
               == ErrorCode::InvalidInputData );
    }

    // Regressions the reordering / new patterns must NOT change.
    // The rank-4 OUTPUT refusal is a different failure class than the
    // fed-input spellings above and stays out of ShapeMismatch.
    CHECK( classifyInferenceError( "ONNX Runtime output 'x' is not 4-D NCHW (the cv::Mat path "
                                   "carries rank-4; use inferNamed for N-D heads)" )
           == InferenceFailureKind::Unknown );
    CHECK( classifyInferenceError( "out of memory" ) == InferenceFailureKind::OutOfMemory );
    CHECK( classifyInferenceError( "cuda_error_out_of_memory" ) == InferenceFailureKind::OutOfMemory );
    CHECK( classifyInferenceError( "inference canceled before the forward pass" )
           == InferenceFailureKind::Canceled );
    CHECK( classifyInferenceError( "worker exited unexpectedly" )
           == InferenceFailureKind::ProviderCrash );
    CHECK( classifyInferenceError( "no runtime provider available for framework 'x' in this build" )
           == InferenceFailureKind::IncompatibleSchema );
    CHECK( classifyInferenceError( "runtime session is not loaded" )
           == InferenceFailureKind::NotLoaded );
}

TEST_CASE( "R15-C C05: a single-seam session refuses a multi-input manifest before any forward",
           "[models][runtime][r15-c]" )
{
    QTemporaryDir dir;
    auto before = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                    .withConstantValue( 1, 1.0f )
                    .writeToDisk( dir.filePath( QStringLiteral( "before.tif" ) ) );
    auto after = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1, GDT_Float32 )
                   .withConstantValue( 1, 2.0f )
                   .writeToDisk( dir.filePath( QStringLiteral( "after.tif" ) ) );
    REQUIRE_FALSE( before.isEmpty() );
    REQUIRE_FALSE( after.isEmpty() );

    ModelInfo model;
    model.name = "r15c-dual";
    model.framework = "r15c-single";
    model.tiling.tileSize = 16;
    ModelInputContract a;
    a.name = "before";
    ModelInputContract b;
    b.name = "after";
    model.inputs = { a, b };
    model.input = a;
    model.output.classes = { "change" };
    model.output.tensorNames = { "change" };

    auto runtime = std::make_shared<SingleSeamRuntime>();
    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;

    bool refused = false;
    try
    {
        engine.runMultiInput( { NamedRasterFeed{ "before", { before.toStdString() }, { 1 } },
                                NamedRasterFeed{ "after", { after.toStdString() }, { 1 } } },
                              dir.filePath( QStringLiteral( "out.tif" ) ).toStdString(), context,
                              {} );
    }
    catch ( const RSOperatorError &e )
    {
        refused = true;
        CHECK( e.code() == ErrorCode::InvalidInputData );
        CHECK_THAT( e.message(), Catch::Matchers::ContainsSubstring( "does not support multi-input" ) );
    }
    CHECK( refused );
}

TEST_CASE( "R15-C C11: a VRAM-budget refusal projects ResourceBudgetExceeded, not InvalidInputData",
           "[models][runtime][r15-c]" )
{
    // Review R1 P1: the F11 mapping was inert (a string match that matched
    // nothing) and untested. This case drives a real over-budget model
    // through runModelInference and pins the projection. Env is restored on
    // every exit path — hardware() re-detects per call.
    struct EnvRestore
    {
        ~EnvRestore()
        {
            qunsetenv( "SICNU_MODEL_GPU" );
            qunsetenv( "SICNU_MODEL_VRAM_MB" );
        }
    } envRestore;
    qputenv( "SICNU_MODEL_GPU", "1" );        // a GPU host...
    qputenv( "SICNU_MODEL_VRAM_MB", "128" );  // ...with a small VRAM budget

    QTemporaryDir dir;
    const QString weights = dir.filePath( QStringLiteral( "big.onnx" ) );
    {
        QFile f( weights );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "big-weights" ) );
    }
    // The input-existence check runs before the readiness gate, so the lane
    // needs a real (tiny) raster to reach the VRAM verdict.
    const QString input = dir.filePath( QStringLiteral( "in.tif" ) );
    REQUIRE_FALSE( sicnu::testing::RsSyntheticRasterBuilder( 8, 8, 1, GDT_Float32 )
                     .withConstantValue( 1, 1.0f )
                     .writeToDisk( input )
                     .isEmpty() );
    std::string regError;
    REQUIRE( ModelCatalog::instance().registerManifestJson(
        R"({"name": "r15c-vram", "task": "segmentation", "framework": "onnx",
            "artifact": {"path": ")" + weights.toStdString() + R"("},
            "runtime": {"gpu": true, "cpu_fallback": false, "estimated_vram_mb": 999999}})",
        dir.filePath( QStringLiteral( "r15c-vram/model.json" ) ).toStdString(), &regError ) );

    ModelExecutionRequest request;
    request.modelReference = "r15c-vram";
    request.inputPath = input.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "out.tif" ) ).toStdString();
    RSOperatorContext context;

    bool refused = false;
    try
    {
        sicnu::operators::runtime::runModelInference( request, context );
    }
    catch ( const sicnu::operators::RSOperatorError &e )
    {
        refused = true;
        // THE ORACLE: a host-budget condition is a resource error, not bad
        // input data.
        CHECK( e.code() == ErrorCode::ResourceBudgetExceeded );
        CHECK_THAT( e.message(), Catch::Matchers::ContainsSubstring( "VRAM" ) );
    }
    CHECK( refused );
    ModelCatalog::instance().unregister( "r15c-vram" );
}
