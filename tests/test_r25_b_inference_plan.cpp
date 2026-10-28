// tests/test_r25_b_inference_plan.cpp — R25-B feature 2: the explainable
// inference plan (sicnu.inference.plan/1) and quality summary
// (sicnu.inference.quality/1).
//
// Contract under test:
//   * plan is a READ-ONLY twin of runModelInference: same refusals, no
//     session acquired (provider factory counter stays zero), no output file
//   * the grid plan (tileSize / gridX×gridY / tilesPlanned / batchSize)
//     matches what a REAL engine run over the same raster reports
//     (metamorphic against the tail-tile geometry)
//   * shared gates: temporal model + single input → plan.contract_unwired;
//     embedding head + labels mode → plan.head_contract_violation; invalid
//     device token; feather blend without halo
//   * quality summary: refuses empty results typed, flags lost tiles
//     (grid_complete false), projects class distribution / detection
//     suppression / EO presence from real stats
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/inference_plan.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>

namespace
{

using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::runtime::DetectionTileStats;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::InferencePlan;
using sicnu::operators::runtime::InferenceQualitySummary;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelExecutionResult;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::RasterOutputMode;
using sicnu::operators::runtime::TileInferenceEngine;
using sicnu::operators::runtime::TileInferenceStats;

std::atomic<int> g_sessionsCreated{ 0 };

class Argmax3Runtime : public IModelRuntime
{
  public:
    Argmax3Runtime( std::string artifact, std::string framework )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) )
    {
    }
    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "r25_b_argmax3"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int N = blob.size[0], C = blob.size[1], H = blob.size[2], W = blob.size[3];
      int dims[4] = { N, 3, H, W };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.0f ) );
      for ( int n = 0; n < N; ++n )
        for ( int y = 0; y < H; ++y )
          for ( int x = 0; x < W; ++x )
          {
            int best = 0;
            float bestV = blob.ptr<float>( n, 0, y )[x];
            for ( int c = 1; c < C; ++c )
            {
              const float v = blob.ptr<float>( n, c, y )[x];
              if ( v > bestV )
              {
                bestV = v;
                best = c;
              }
            }
            out.ptr<float>( n, best, y )[x] = 1.0f;
          }
      return out;
    }

  private:
    std::string m_artifact;
    std::string m_framework;
};

struct ProviderGuard
{
    ProviderGuard()
    {
      g_sessionsCreated = 0;
      ModelRuntimeRegistry::instance().releaseAll();
      ModelRuntimeRegistry::instance().registerProvider(
        "r25-b-plan",
        []( const ModelInfo &model, const ModelHardwareCapabilities &, std::string *error )
          -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.empty() )
          {
            if ( error )
              *error = "no resolved artifact";
            return nullptr;
          }
          ++g_sessionsCreated;
          return std::make_shared<Argmax3Runtime>( model.resolvedArtifactPath, "r25-b-plan" );
        } );
    }
    ~ProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

ModelInfo planModel()
{
  ModelInfo info;
  info.name = "r25-b-plan";
  info.task = "segmentation";
  info.framework = "r25-b-plan";
  info.readiness = ModelReadiness::Ready;
  info.tiling.tileSize = 16;
  info.output.classes = { "a", "b", "c" };
  info.output.format = "labels";
  info.resolvedArtifactPath = "r25-b-plan-weights";
  return info;
}

ModelExecutionRequest singleInputRequest( const std::string &input, const std::string &output )
{
  ModelExecutionRequest request;
  request.inputPath = input;
  request.modelReference = "r25-b-plan";
  request.outputPath = output;
  request.outputMode = RasterOutputMode::Labels;
  return request;
}

} // namespace

TEST_CASE( "plan matches a real engine run on tail-tile geometry (metamorphic)",
           "[r25_b][inference_plan]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "grid-in.tif" ) );
  // 96×64 with tile size 16 → 6×4 = 24 tiles, no partial edges; a second
  // run below uses a tail-tile geometry to cover both cases.
  sicnu::testing::RsSyntheticRasterBuilder( 96, 64, 3, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const std::string output = dir.filePath( QStringLiteral( "grid-out.tif" ) ).toStdString();

  const ModelInfo model = planModel();
  const InferencePlan plan =
    sicnu::operators::runtime::planModelInference( singleInputRequest( input.toStdString(), output ),
                                                    model );

  CHECK( plan.verdict == InferencePlan::Verdict::Ready );
  CHECK( plan.tileSize == 16 );
  CHECK( plan.gridX == 6 );
  CHECK( plan.gridY == 4 );
  CHECK( plan.tilesPlanned == 24 );
  CHECK( plan.batchSize >= 1 );
  CHECK( plan.batchesEstimated ==
         ( plan.tilesPlanned + plan.batchSize - 1 ) / plan.batchSize );

  // READ-ONLY: no output, no session.
  CHECK( g_sessionsCreated == 0 );
  CHECK_FALSE( QFileInfo::exists( QString::fromStdString( output ) ) );

  // The REAL run over the same raster must report the same skeleton.
  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  CHECK( g_sessionsCreated == 1 ); // the engine acquired; the plan did not
  const TileInferenceStats stats = engine.run( input.toStdString(), {}, output, context );
  CHECK( stats.tilesPlanned == plan.tilesPlanned );
  CHECK( stats.tileSize == plan.tileSize );
  CHECK( stats.batchSize == plan.batchSize );

  // Quality summary over the finished result: full coverage, complete grid.
  ModelExecutionResult result;
  result.rasterStats = stats;
  const InferenceQualitySummary summary =
    sicnu::operators::runtime::summarizeInferenceQuality( result );
  CHECK( summary.tilesProcessed == summary.tilesPlanned );
  CHECK( summary.tileCoverageRatio == Catch::Approx( 1.0 ) );
  CHECK( summary.gridComplete );
  CHECK( summary.hasClassDistribution );
  CHECK( summary.classifiedPixelRatio == Catch::Approx( 1.0 ) );
  CHECK( summary.toJson()["schema"].asString() == "sicnu.inference.quality/1" );
}

TEST_CASE( "plan predicts tail-tile grids the engine agrees with",
           "[r25_b][inference_plan]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "tail-in.tif" ) );
  // 50×35 with tile 16 → ceil(50/16)=4 × ceil(35/16)=3 = 12 tiles (tails).
  sicnu::testing::RsSyntheticRasterBuilder( 50, 35, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const std::string output = dir.filePath( QStringLiteral( "tail-out.tif" ) ).toStdString();

  const ModelInfo model = planModel();
  const InferencePlan plan =
    sicnu::operators::runtime::planModelInference( singleInputRequest( input.toStdString(), output ),
                                                    model );

  REQUIRE( plan.verdict == InferencePlan::Verdict::Ready );
  CHECK( plan.gridX == 4 );
  CHECK( plan.gridY == 3 );
  CHECK( plan.tilesPlanned == 12 );

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  const TileInferenceStats stats = engine.run( input.toStdString(), {}, output, context );
  CHECK( stats.tilesPlanned == plan.tilesPlanned );
}

TEST_CASE( "plan shares the run seam's typed refusals", "[r25_b][inference_plan]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "gates-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 3, GDT_Float32 ).writeToDisk( input );
  const std::string output = dir.filePath( QStringLiteral( "gates-out.tif" ) ).toStdString();

  SECTION( "missing input" )
  {
    const InferencePlan plan = sicnu::operators::runtime::planModelInference(
      singleInputRequest( "/nonexistent/raster.tif", output ), planModel() );
    CHECK( plan.verdict == InferencePlan::Verdict::Refused );
    CHECK( plan.findingCode( "plan.input_missing" ) != nullptr );
  }

  SECTION( "unresolvable model reference (resolving overload)" )
  {
    ModelExecutionRequest request =
      singleInputRequest( input.toStdString(), output );
    request.modelReference = "no-such-model-anywhere";
    const InferencePlan plan = sicnu::operators::runtime::planModelInference( request );
    CHECK( plan.verdict == InferencePlan::Verdict::Refused );
    CHECK( plan.findingCode( "plan.model_not_ready" ) != nullptr );
  }

  SECTION( "temporal model on the single-input path" )
  {
    ModelInfo model = planModel();
    sicnu::operators::ModelInputContract temporal;
    temporal.name = "frames";
    temporal.temporalLength = 4;
    model.inputs = { temporal };
    const InferencePlan plan = sicnu::operators::runtime::planModelInference(
      singleInputRequest( input.toStdString(), output ), model );
    CHECK( plan.verdict == InferencePlan::Verdict::Refused );
    const auto *finding = plan.findingCode( "plan.contract_unwired" );
    REQUIRE( finding != nullptr );
    CHECK( finding->message.find( "temporal" ) != std::string::npos );
    CHECK_FALSE( finding->remediation.empty() );
  }

  SECTION( "embedding head cannot be collapsed to labels" )
  {
    ModelInfo model = planModel();
    sicnu::operators::ModelHeadContract head;
    head.name = "embedding_out";
    head.role = "embedding";
    model.output.heads = { head };
    const InferencePlan plan = sicnu::operators::runtime::planModelInference(
      singleInputRequest( input.toStdString(), output ), model );
    CHECK( plan.verdict == InferencePlan::Verdict::Refused );
    CHECK_FALSE( plan.headContractOk );
    CHECK( plan.findingCode( "plan.head_contract_violation" ) != nullptr );
    CHECK( plan.headContractNote.find( "embedding" ) != std::string::npos );
  }

  SECTION( "unparsable device token" )
  {
    ModelExecutionRequest request = singleInputRequest( input.toStdString(), output );
    request.deviceToken = "gpu9";
    const InferencePlan plan =
      sicnu::operators::runtime::planModelInference( request, planModel() );
    CHECK( plan.verdict == InferencePlan::Verdict::Refused );
    CHECK( plan.findingCode( "plan.device_unparsable" ) != nullptr );
  }

  SECTION( "feather blend without halo" )
  {
    ModelInfo model = planModel();
    model.tiling.blend = "feather";
    const InferencePlan plan = sicnu::operators::runtime::planModelInference(
      singleInputRequest( input.toStdString(), output ), model );
    CHECK( plan.verdict == InferencePlan::Verdict::Refused );
    CHECK( plan.findingCode( "plan.blend_without_halo" ) != nullptr );
  }
}

TEST_CASE( "plan mirrors the engine gates R1 closed: dtype, roles, channels, "
           "mode conflicts, detection knobs, scene lane",
           "[r25_b][inference_plan][r1]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "r1-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 3, GDT_Float32 ).writeToDisk( input );
  const std::string output = dir.filePath( QStringLiteral( "r1-out.tif" ) ).toStdString();

  SECTION( "dtype contract" )
  {
    ModelInfo model = planModel();
    model.input.dtype = "uint8"; // raster bands are Float32
    const InferencePlan plan = sicnu::operators::runtime::planModelInference(
      singleInputRequest( input.toStdString(), output ), model );
    CHECK( plan.verdict == InferencePlan::Verdict::Refused );
    CHECK( plan.findingCode( "plan.dtype_mismatch" ) != nullptr );
  }

  SECTION( "band roles arity" )
  {
    ModelInfo model = planModel();
    model.input.bandRoles = { "red", "green" }; // 2 roles, 3 bands fed
    const InferencePlan plan = sicnu::operators::runtime::planModelInference(
      singleInputRequest( input.toStdString(), output ), model );
    CHECK( plan.verdict == InferencePlan::Verdict::Refused );
    CHECK( plan.findingCode( "plan.band_roles_arity" ) != nullptr );
  }

  SECTION( "manifest-derived mode conflicts (uncertainty + derived format)" )
  {
    ModelInfo model = planModel();
    model.output.format = "labels";
    model.output.uncertainty = "entropy";
    // request leaves the default Probability: the engine derives `labels`
    // from the manifest and refuses the uncertainty conflict.
    const InferencePlan plan = sicnu::operators::runtime::planModelInference(
      singleInputRequest( input.toStdString(), output ), model );
    CHECK( plan.verdict == InferencePlan::Verdict::Refused );
    CHECK( plan.findingCode( "plan.output_mode_conflict" ) != nullptr );
  }

  SECTION( "detection knob bounds" )
  {
    ModelExecutionRequest request = singleInputRequest( input.toStdString(), output );
    request.asDetection = true;
    request.confOverride = 2.5;
    const InferencePlan plan =
      sicnu::operators::runtime::planModelInference( request, planModel() );
    CHECK( plan.findingCode( "plan.conf_bounds" ) != nullptr );
  }

  SECTION( "scene lane plans no tile grid" )
  {
    ModelExecutionRequest request = singleInputRequest( input.toStdString(), output );
    request.asSceneClassification = true;
    const InferencePlan plan =
      sicnu::operators::runtime::planModelInference( request, planModel() );
    CHECK( plan.tilesPlanned == 0 );
    CHECK( plan.findingCode( "plan.scene_single_pass" ) != nullptr );
  }

  SECTION( "multi-input temporal accounting feeds the batch budget" )
  {
    ModelInfo model = planModel();
    model.inputs.clear();
    sicnu::operators::ModelInputContract temporal;
    temporal.name = "frames";
    temporal.temporalLength = 3;
    model.inputs = { temporal };

    ModelExecutionRequest request;
    request.modelReference = "r25-b-plan";
    request.outputPath = output;
    sicnu::operators::runtime::NamedRasterFeed feed;
    feed.name = "frames";
    feed.paths = { input.toStdString() };
    request.namedInputs = { feed };

    const InferencePlan plan =
      sicnu::operators::runtime::planModelInference( request, model );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Ready );
    REQUIRE( plan.inputs.size() == 1 );
    // 3 raster bands × DECLARED 3 frames = 9 channels in the budget — the
    // engine's admission accounting, not the provided frame count (1).
    CHECK( plan.inputs.front().frames == 3 );
    CHECK( plan.fedChannels == 9 );
    CHECK( plan.toJson()["tiling"]["fed_channels"].asInt() == 9 );
  }
}

TEST_CASE( "quality summary refuses empty results and flags lost tiles",
           "[r25_b][inference_quality]" )
{
  ModelExecutionResult empty;
  REQUIRE_THROWS_AS( sicnu::operators::runtime::summarizeInferenceQuality( empty ),
                     sicnu::operators::RSOperatorError );

  // Lost tiles (r24-B WP-B2 invariant): processed + skipped != planned.
  ModelExecutionResult lost;
  lost.rasterStats.tilesPlanned = 10;
  lost.rasterStats.tilesProcessed = 7;
  lost.rasterStats.tilesSkippedNoData = 1;
  lost.rasterStats.outWidth = 32;
  lost.rasterStats.outHeight = 32;
  lost.rasterStats.outBands = 1;
  const InferenceQualitySummary lostSummary =
    sicnu::operators::runtime::summarizeInferenceQuality( lost );
  CHECK_FALSE( lostSummary.gridComplete );
  CHECK( lostSummary.tileCoverageRatio == Catch::Approx( 0.7 ) );
  bool flagged = false;
  for ( const auto &note : lostSummary.notes )
    flagged = flagged || note.code == "quality.grid_incomplete";
  CHECK( flagged );

  // Detection stats: suppression ratio from raw vs kept.
  ModelExecutionResult detection;
  detection.detectionStats.tilesPlanned = 4;
  detection.detectionStats.tilesProcessed = 4;
  detection.detectionStats.rawDetections = 100;
  detection.detectionStats.detectionsKept = 25;
  const InferenceQualitySummary detectionSummary =
    sicnu::operators::runtime::summarizeInferenceQuality( detection );
  CHECK( detectionSummary.isDetection );
  CHECK( detectionSummary.suppressionRatio == Catch::Approx( 0.75 ) );
  CHECK( detectionSummary.gridComplete );

  // Class distribution: dominant class + classified ratio.
  ModelExecutionResult labeled;
  labeled.rasterStats.tilesPlanned = 2;
  labeled.rasterStats.tilesProcessed = 2;
  labeled.rasterStats.outWidth = 10;
  labeled.rasterStats.outHeight = 10;
  labeled.rasterStats.outBands = 1;
  labeled.rasterStats.classPixelCounts = { 10, 80, 10 };
  const InferenceQualitySummary labeledSummary =
    sicnu::operators::runtime::summarizeInferenceQuality( labeled );
  CHECK( labeledSummary.hasClassDistribution );
  CHECK( labeledSummary.dominantClass == 1 );
  CHECK( labeledSummary.classifiedPixelRatio == Catch::Approx( 1.0 ) );
}
