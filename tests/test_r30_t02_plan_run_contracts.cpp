// test_r30_t02_plan_run_contracts.cpp — R30-T02 WP01/WP02/WP11: the unified
// execution-contract resolver makes the inference PLAN and the tile ENGINES
// answer identically for the same (model, request). This matrix pins:
//   * labels/mask + morphology: plan Ready AND the engine EXECUTES it (the
//     historical plan gate was inverted — it refused exactly the products
//     morphology runs on);
//   * probability + morphology: both refuse (the engine used to silently
//     skip it while provenance still claimed it);
//   * single-input pad/clamp, calibration lane rules, feather+pad on the
//     multi-input lane, derived formats on multi-input: both refuse with the
//     same message;
//   * provenance claims exactly what executed (resolved blend, executed
//     morphology; a blend=none override over a feather manifest claims none).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/inference_plan.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>
#include <string>
#include <vector>

#include <QDir>

namespace
{

using namespace sicnu::operators::runtime;
using sicnu::operators::ErrorCode;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelInputContract;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

/// Single-input fake: TWO class planes whose probabilities follow the input
/// blob's first channel (plane 0 = 1-v, plane 1 = v) — argmax produces a
/// threshold pattern with real spatial structure, so labels/mask products and
/// the morphology pass have something true to do.
class TwoClassThresholdRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "r30-matrix"; }
    std::string backendName() const override { return "r30matrix"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r30matrix"; }

    cv::Mat infer( const cv::Mat &nchw ) override
    {
      const int B = nchw.size[0];
      const int H = nchw.size[2];
      const int W = nchw.size[3];
      int dims[4] = { B, 2, H, W };
      cv::Mat out( 4, dims, CV_32F );
      for ( int b = 0; b < B; ++b )
        for ( int row = 0; row < H; ++row )
          for ( int col = 0; col < W; ++col )
          {
            const float v = nchw.ptr<float>( b, 0 )[row * W + col];
            out.ptr<float>( b, 0 )[row * W + col] = 1.0f - v;
            out.ptr<float>( b, 1 )[row * W + col] = v;
          }
      return out;
    }
};

/// Base single-input model for the matrix; knobs applied per case.
ModelInfo matrixModel()
{
  ModelInfo model;
  model.name = "r30-matrix";
  model.framework = "r30-matrix";
  model.task = "segmentation";
  model.tiling.tileSize = 16;
  model.output.classes = { "background", "foreground" };
  return model;
}

/// Two named feeds on a 32x16 grid (co-registered by construction).
ModelInfo matrixMultiModel()
{
  ModelInfo model = matrixModel();
  model.name = "r30-matrix-multi";
  model.tiling.halo = 4;
  ModelInputContract a;
  a.name = "A";
  ModelInputContract b;
  b.name = "B";
  model.inputs = { a, b };
  model.input = a;
  return model;
}

QString writeStepRaster( const QTemporaryDir &dir, const QString &name, float left, float right )
{
  // 32x16 georeferenced step raster, step at x=16: two tile columns at tile
  // size 16 (tail-tile geometry exercised by the 32x16 grid).
  sicnu::testing::RsSyntheticRasterBuilder builder( 32, 16, 1, GDT_Float32 );
  builder.withCrs( QStringLiteral( "EPSG:32648" ) )
      .withGeoTransform( 500000.0, 10.0, 4400000.0, -10.0 )
      .withRect( 1, 0, 0, 15, 15, left )
      .withRect( 1, 16, 0, 31, 15, right );
  const QString path = builder.writeToDisk( dir.filePath( name ) );
  REQUIRE_FALSE( path.isEmpty() );
  return path;
}

/// Registers the fake framework so the PLAN's runtime-readiness gate sees a
/// provider (the engine itself never loads it in the refusal cases).
struct MatrixProviderGuard
{
  MatrixProviderGuard()
  {
    ModelRuntimeRegistry::instance().registerProvider(
      "r30-matrix",
      []( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) {
        struct NullRuntime final : IModelRuntime
        {
          std::string framework() const override { return "r30-matrix"; }
          std::string backendName() const override { return "matrix"; }
          std::string deviceName() const override { return "cpu"; }
          std::string artifactPath() const override { return "fake://matrix"; }
          cv::Mat infer( const cv::Mat &nchw ) override { return nchw.clone(); }
        };
        return ModelRuntimePtr( std::make_shared<NullRuntime>() );
      },
      ProviderTraits{} );
  }
  ~MatrixProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

QJsonObject readProvenance( const QString &outputPath )
{
  QFile prov( outputPath + QStringLiteral( ".prov.json" ) );
  REQUIRE( prov.open( QIODevice::ReadOnly ) );
  return QJsonDocument::fromJson( prov.readAll() ).object();
}

} // namespace

// ---------------------------------------------------------------------------

TEST_CASE( "R30-T02: labels + morphology is planned Ready and executed",
           "[models][r30t02][contracts]" )
{
  MatrixProviderGuard providerGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = writeStepRaster( dir, QStringLiteral( "step.tif" ), 0.0f, 1.0f );

  ModelInfo model = matrixModel();
  model.output.format = "labels";
  model.postprocess.morphology = "open";
  model.postprocess.morphologyKernelPx = 3;

  // PLAN: Ready, and no postprocess conflict finding (the historical gate
  // refused exactly this combination).
  ModelExecutionRequest planRequest;
  planRequest.modelReference = "r30-matrix";
  planRequest.inputPath = raster.toStdString();
  planRequest.outputPath = dir.filePath( QStringLiteral( "labels.tif" ) ).toStdString();
  const InferencePlan plan = planModelInference( planRequest, model );
  CHECK( plan.verdict == InferencePlan::Verdict::Ready );
  CHECK( plan.findingCode( "plan.postprocess_contract_conflict" ) == nullptr );
  CHECK( plan.outputMode == RasterOutputMode::Labels );

  // RUN: the engine executes (single band, labels domain, morphology pass).
  auto runtime = std::make_shared<TwoClassThresholdRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "labels.tif" ) );
  const TileInferenceStats stats = engine.run( raster.toStdString(), {}, out.toStdString(), context );
  CHECK( stats.tilesProcessed == 2 ); // 32x16 at tile 16: 2x1 grid

  // PRODUCT + CLAIMS: single-band Byte labels; the provenance sidecar claims
  // exactly the executed morphology (WP11).
  int w = 0, h = 0;
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( out.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds );
  CHECK( ds->GetRasterCount() == 1 );
  CHECK( ds->GetRasterBand( 1 )->GetRasterDataType() == GDT_Byte );
  GDALClose( ds );
  const QJsonObject prov = readProvenance( out );
  CHECK( prov.value( QStringLiteral( "execution" ) )
           .toObject()
           .value( QStringLiteral( "morphology" ) )
           .toString() == QStringLiteral( "open" ) );
}

TEST_CASE( "R30-T02: probability + morphology refuses in plan AND engine",
           "[models][r30t02][contracts]" )
{
  MatrixProviderGuard providerGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = writeStepRaster( dir, QStringLiteral( "step.tif" ), 0.0f, 1.0f );

  ModelInfo model = matrixModel();
  model.postprocess.morphology = "open"; // no output.format: probability stack
  model.postprocess.morphologyKernelPx = 3;

  ModelExecutionRequest planRequest;
  planRequest.modelReference = "r30-matrix";
  planRequest.inputPath = raster.toStdString();
  planRequest.outputPath = dir.filePath( QStringLiteral( "prob.tif" ) ).toStdString();
  const InferencePlan plan = planModelInference( planRequest, model );
  REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
  const auto *finding = plan.findingCode( "plan.postprocess_contract_conflict" );
  REQUIRE( finding );
  CHECK( finding->message.find( "morphology executes on labels/mask products" )
         != std::string::npos );

  // The engine refuses with the same contract (message parity).
  auto runtime = std::make_shared<TwoClassThresholdRuntime>();
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "prob.tif" ) );
  REQUIRE_THROWS_WITH(
    engine.run( raster.toStdString(), {}, out.toStdString(), context ),
    Catch::Matchers::ContainsSubstring( "morphology executes on labels/mask products" ) );
  CHECK_FALSE( QFile::exists( out ) );
}

TEST_CASE( "R30-T02: single-input pad and calibration lane rules match",
           "[models][r30t02][contracts]" )
{
  MatrixProviderGuard providerGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = writeStepRaster( dir, QStringLiteral( "step.tif" ), 0.0f, 1.0f );
  auto runtime = std::make_shared<TwoClassThresholdRuntime>();

  SECTION( "global preprocess.pad refuses in plan and engine" )
  {
    ModelInfo model = matrixModel();
    model.preprocess.pad = 2;

    ModelExecutionRequest request;
    request.modelReference = "r30-matrix";
    request.inputPath = raster.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "pad.tif" ) ).toStdString();
    const InferencePlan plan = planModelInference( request, model );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.preprocess_lane_conflict" ) );

    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;
    REQUIRE_THROWS_WITH( engine.run( raster.toStdString(), {},
                                     dir.filePath( QStringLiteral( "pad.tif" ) ).toStdString(),
                                     context ),
                         Catch::Matchers::ContainsSubstring( "executed by the multi-input" ) );
  }

  SECTION( "calibration without a derived format refuses in plan and engine" )
  {
    ModelInfo model = matrixModel();
    model.postprocess.calibrationTemperature = 1.5;

    ModelExecutionRequest request;
    request.modelReference = "r30-matrix";
    request.inputPath = raster.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "cal.tif" ) ).toStdString();
    const InferencePlan plan = planModelInference( request, model );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.postprocess_contract_conflict" ) );

    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;
    REQUIRE_THROWS_WITH(
      engine.run( raster.toStdString(), {},
                  dir.filePath( QStringLiteral( "cal.tif" ) ).toStdString(), context ),
      Catch::Matchers::ContainsSubstring( "calibration_temperature executes on labels/mask" ) );
  }

  SECTION( "calibration + feather on the derived path refuses in plan and engine" )
  {
    ModelInfo model = matrixModel();
    model.output.format = "labels";
    model.postprocess.calibrationTemperature = 1.5;
    model.tiling.blend = "feather";
    model.tiling.halo = 4; // feather is admissible on its own

    ModelExecutionRequest request;
    request.modelReference = "r30-matrix";
    request.inputPath = raster.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "calf.tif" ) ).toStdString();
    const InferencePlan plan = planModelInference( request, model );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.postprocess_contract_conflict" ) );

    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;
    REQUIRE_THROWS_WITH(
      engine.run( raster.toStdString(), {},
                  dir.filePath( QStringLiteral( "calf.tif" ) ).toStdString(), context ),
      Catch::Matchers::ContainsSubstring( "not executed on the feather-blended derived path" ) );
  }

  SECTION( "labels + mask_threshold still refuses in plan and engine" )
  {
    ModelInfo model = matrixModel();
    model.output.format = "labels";
    model.postprocess.maskThreshold = 0.5f;

    ModelExecutionRequest request;
    request.modelReference = "r30-matrix";
    request.inputPath = raster.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "mt.tif" ) ).toStdString();
    const InferencePlan plan = planModelInference( request, model );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.output_mode_conflict" ) );

    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;
    REQUIRE_THROWS_WITH(
      engine.run( raster.toStdString(), {},
                  dir.filePath( QStringLiteral( "mt.tif" ) ).toStdString(), context ),
      Catch::Matchers::ContainsSubstring( "mask_threshold is meaningless" ) );
  }
}

TEST_CASE( "R30-T02: multi-input lane refusals match between plan and engine",
           "[models][r30t02][contracts]" )
{
  MatrixProviderGuard providerGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString rasterA = writeStepRaster( dir, QStringLiteral( "a.tif" ), 0.0f, 1.0f );
  const QString rasterB = writeStepRaster( dir, QStringLiteral( "b.tif" ), 1.0f, 0.0f );

  auto runtime = std::make_shared<TwoClassThresholdRuntime>();

  const auto feedsFor = [&] {
    return std::vector<NamedRasterFeed>{
      { "A", { rasterA.toStdString() }, {}, {}, {}, {} },
      { "B", { rasterB.toStdString() }, {}, {}, {}, {} },
    };
  };
  const auto planFor = [ & ]( const ModelInfo &model, TileBlend blend ) {
    ModelExecutionRequest request;
    request.modelReference = model.name;
    request.outputPath = dir.filePath( QStringLiteral( "multi.tif" ) ).toStdString();
    request.namedInputs = feedsFor();
    request.blend = blend;
    return planModelInference( request, model );
  };
  const auto runExpectRefusal = [ & ]( const ModelInfo &model, TileBlend blend,
                                       const char *fragment ) {
    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;
    TileInferenceRunOptions options;
    options.blend = blend;
    REQUIRE_THROWS_WITH( engine.runMultiInput( feedsFor(),
                                               dir.filePath( QStringLiteral( "multi.tif" ) )
                                                 .toStdString(),
                                               context, options ),
                         Catch::Matchers::ContainsSubstring( fragment ) );
  };

  SECTION( "feather + pad refuses in plan (was engine-only) and engine" )
  {
    ModelInfo model = matrixMultiModel();
    model.tiling.blend = "feather";
    model.preprocess.pad = 1; // zero-pad frame the accumulator cannot blend

    const InferencePlan plan = planFor( model, TileBlend::Unset );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.blend_pad_conflict" ) );

    runExpectRefusal( model, TileBlend::Unset, "not supported with preprocess.pad" );
  }

  SECTION( "per-feed pad under feather refuses in plan and engine" )
  {
    ModelInfo model = matrixMultiModel();
    model.tiling.blend = "feather";
    ModelInputContract a;
    a.name = "A";
    a.preprocessDeclared = true;
    a.preprocess.pad = 1;
    model.inputs[0] = a;

    const InferencePlan plan = planFor( model, TileBlend::Unset );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.blend_pad_conflict" ) );

    runExpectRefusal( model, TileBlend::Unset, "not supported with preprocess.pad" );
  }

  SECTION( "derived output format refuses in plan (was engine-only) and engine" )
  {
    ModelInfo model = matrixMultiModel();
    model.output.format = "labels";

    const InferencePlan plan = planFor( model, TileBlend::Unset );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.multi_input_contract" ) );

    runExpectRefusal( model, TileBlend::Unset, "probability stack only" );
  }

  SECTION( "morphology can never execute on the multi-input lane" )
  {
    ModelInfo model = matrixMultiModel();
    model.postprocess.morphology = "open";
    model.postprocess.morphologyKernelPx = 3;

    const InferencePlan plan = planFor( model, TileBlend::Unset );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.postprocess_contract_conflict" ) );

    runExpectRefusal( model, TileBlend::Unset, "never execute on the multi-input" );
  }

  SECTION( "feather without halo still refuses on both sides" )
  {
    ModelInfo model = matrixMultiModel();
    model.tiling.halo = 0;
    model.tiling.blend = "feather";

    const InferencePlan plan = planFor( model, TileBlend::Unset );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.blend_without_halo" ) );

    runExpectRefusal( model, TileBlend::Unset, "feather requires a halo" );
  }
}

TEST_CASE( "R30-T02: provenance claims the resolved blend, not the manifest's",
           "[models][r30t02][contracts]" )
{
  MatrixProviderGuard providerGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = writeStepRaster( dir, QStringLiteral( "step.tif" ), 0.0f, 1.0f );

  ModelInfo model = matrixModel();
  model.tiling.blend = "feather";
  model.tiling.halo = 4;

  auto runtime = std::make_shared<TwoClassThresholdRuntime>();

  // blend=none override: hard-stitched product must NOT claim feather.
  TileInferenceRunOptions options;
  options.blend = TileBlend::None;
  TileInferenceEngine engine( model, runtime );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "stitched.tif" ) );
  engine.run( raster.toStdString(), {}, out.toStdString(), context, options );
  const QJsonObject prov = readProvenance( out );
  CHECK_FALSE( prov.value( QStringLiteral( "execution" ) )
                 .toObject()
                 .contains( QStringLiteral( "blend" ) ) );

  // Unset (manifest feather): the blended product claims feather.
  const QString blended = dir.filePath( QStringLiteral( "blended.tif" ) );
  TileInferenceEngine engine2( model, runtime );
  RSOperatorContext context2;
  engine2.run( raster.toStdString(), {}, blended.toStdString(), context2 );
  const QJsonObject prov2 = readProvenance( blended );
  CHECK( prov2.value( QStringLiteral( "execution" ) )
           .toObject()
           .value( QStringLiteral( "blend" ) )
           .toString() == QStringLiteral( "feather" ) );
}
