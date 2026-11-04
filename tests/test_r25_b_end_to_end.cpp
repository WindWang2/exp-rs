// tests/test_r25_b_end_to_end.cpp — R25-B W7: one production-path chain
// across the three deepened seams, no mocks between the layers:
//
//   preflight-explain → registerSource (production GDAL provider) →
//   planModelInference (read-only dry run) → TileInferenceEngine.run →
//   summarizeInferenceQuality → ExperimentRunRecorder (real SQLite store) →
//   ReplayDeviationAnalyzer v2 (typed deviation entries)
//
// Only the model runtime is a registered fake (the build has no ONNX/TRT);
// every other hop is production code. The chain proves the data identity
// survives into the model runtime input and the inference output lands in
// the experiment record with a digest.
#include <catch2/catch_test_macros.hpp>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/import_preflight.h"
#include "data/source_descriptor.h"

#include "dataset/dataset_ids.h"

#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/replay_deviation.h"
#include "experiment/run_recorder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/inference_plan.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <QTemporaryDir>

#include <atomic>

namespace
{

using sicnu::data::DataManager;
using sicnu::data::ImportPreflight;
using sicnu::data::ImportPreflightReport;
using sicnu::data::ImportPreflightVerdict;
using sicnu::data::PersistencePolicy;
using sicnu::data::RegisterRequest;
using sicnu::experiment::Experiment;
using sicnu::experiment::ExperimentRunRecorder;
using sicnu::experiment::ExperimentStore;
using sicnu::experiment::ReplayDeviationAnalyzer;
using sicnu::experiment::ReplayDeviationDomain;
using sicnu::experiment::ReplayDeviationReport;
using sicnu::experiment::RunEnvironment;
using sicnu::experiment::RunStartRequest;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::InferencePlan;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::RasterOutputMode;
using sicnu::operators::runtime::TileInferenceEngine;

class Argmax3Runtime : public IModelRuntime
{
  public:
    Argmax3Runtime( std::string artifact, std::string framework )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) )
    {
    }
    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "r25_b_e2e"; }
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
      ModelRuntimeRegistry::instance().releaseAll();
      ModelRuntimeRegistry::instance().registerProvider(
        "r25-b-e2e",
        []( const ModelInfo &model, const ModelHardwareCapabilities &, std::string *error )
          -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.empty() )
          {
            if ( error )
              *error = "no resolved artifact";
            return nullptr;
          }
          return std::make_shared<Argmax3Runtime>( model.resolvedArtifactPath, "r25-b-e2e" );
        } );
    }
    ~ProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

ModelInfo e2eModel()
{
  ModelInfo info;
  info.name = "r25-b-e2e";
  info.task = "segmentation";
  info.framework = "r25-b-e2e";
  info.readiness = ModelReadiness::Ready;
  info.tiling.tileSize = 16;
  info.output.classes = { "a", "b", "c" };
  info.output.format = "labels";
  info.resolvedArtifactPath = "r25-b-e2e-weights";
  return info;
}

} // namespace

TEST_CASE( "import preflight to replay deviation: one chain, production seams",
           "[r25_b][end_to_end]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;

  // 1. Preflight-explain a fresh raster (F1): importable, not registered yet.
  const QString input = dir.filePath( QStringLiteral( "scene.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 48, 32, 3, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );

  DataManager manager;
  const ImportPreflightReport before = ImportPreflight::inspect( input, &manager );
  CHECK( before.verdict == ImportPreflightVerdict::Importable );
  CHECK_FALSE( before.alreadyRegistered );

  // 2. Register through the production GDAL provider; the preflight now
  //    predicts the reuse (dedup) without registering anything itself.
  RegisterRequest request;
  request.source.providerKey = QStringLiteral( "gdal" );
  request.source.canonicalSource = input;
  request.persistence = PersistencePolicy::ProjectPersistent;
  const auto registered = manager.registerSource( request );
  REQUIRE_FALSE( registered.assetId.isNull() );

  const ImportPreflightReport after = ImportPreflight::inspect( input, &manager );
  CHECK( after.alreadyRegistered );
  CHECK( after.existingAssetId == registered.assetId.toString() );
  CHECK( manager.assets().size() == 1 );

  // 3. Plan the inference over the registered source (F2): read-only, the
  //    grid matches the run that follows.
  ModelExecutionRequest exec;
  exec.inputPath = input.toStdString();
  exec.modelReference = "r25-b-e2e";
  exec.outputPath = dir.filePath( QStringLiteral( "labels.tif" ) ).toStdString();
  exec.outputMode = RasterOutputMode::Labels;
  const InferencePlan plan = sicnu::operators::runtime::planModelInference( exec, e2eModel() );
  REQUIRE( plan.verdict == InferencePlan::Verdict::Ready );
  CHECK( plan.tilesPlanned == 3 * 2 );

  // 4. Run the inference through the production engine; the output product
  //    feeds the experiment record with its digest.
  const ModelInfo model = e2eModel();
  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  const auto stats = engine.run( input.toStdString(), {}, exec.outputPath, context );
  REQUIRE( stats.tilesProcessed == stats.tilesPlanned );
  CHECK( stats.tilesPlanned == plan.tilesPlanned );

  sicnu::operators::runtime::ModelExecutionResult result;
  result.rasterStats = stats;
  const auto quality = sicnu::operators::runtime::summarizeInferenceQuality( result );
  CHECK( quality.gridComplete );
  CHECK( quality.hasClassDistribution );

  // 5. Record the experiment (real store) and replay with a drifted model
  //    digest: the v2 analyzer attributes the deviation to the MODEL domain.
  ExperimentStore store;
  REQUIRE( store.open( dir.filePath( QStringLiteral( "experiment.sqlite" ) ) ) );
  Experiment experiment;
  experiment.setExperimentId( QStringLiteral( "e-e2e" ) );
  experiment.setName( QStringLiteral( "e-e2e" ) );
  REQUIRE( store.upsertExperiment( experiment ).has_value() );

  ExperimentRunRecorder recorder( store );
  auto makeRequest = []( const QString &ref, const QString &modelDigest )
  {
    RunStartRequest runRequest;
    runRequest.experimentId = QStringLiteral( "e-e2e" );
    runRequest.algorithmId = QStringLiteral( "rs:infer" );
    runRequest.algorithmVersion = QStringLiteral( "1" );
    runRequest.parameters = QJsonObject{ { QStringLiteral( "model" ), QStringLiteral( "r25-b-e2e" ) } };
    runRequest.datasetVersionId = sicnu::dataset::DatasetVersionId::generate().toString();
    runRequest.datasetFingerprint = QStringLiteral( "fp-e2e" );
    runRequest.modelId = QStringLiteral( "r25-b-e2e" );
    runRequest.modelDigest = modelDigest;
    runRequest.seed = 7;
    runRequest.executionRef = ref;
    runRequest.environment = RunEnvironment::fromFields(
      QJsonObject{ { QStringLiteral( "platform" ), QStringLiteral( "test" ) } } );
    return runRequest;
  };
  sicnu::experiment::ExperimentRun::Artifact artifact;
  artifact.path = QStringLiteral( "labels.tif" );
  artifact.role = QStringLiteral( "primary" );
  artifact.digest = QStringLiteral( "digest-labels" );
  artifact.sizeBytes = 128;
  const QJsonObject metrics{ { QStringLiteral( "coverage" ), 1.0 } };

  auto originalId = recorder.startRun( makeRequest( QStringLiteral( "exec-1" ),
                                                    QStringLiteral( "digest-weights-1" ) ) );
  REQUIRE( originalId.has_value() );
  REQUIRE( recorder.markSucceeded( originalId.value(), { artifact }, metrics ).has_value() );
  auto replayId = recorder.startRun( makeRequest( QStringLiteral( "exec-2" ),
                                                  QStringLiteral( "digest-weights-2" ) ) );
  REQUIRE( replayId.has_value() );
  REQUIRE( recorder.markSucceeded( replayId.value(), { artifact }, metrics ).has_value() );

  ReplayDeviationAnalyzer analyzer( store );
  const auto report = analyzer.analyze( originalId.value(), replayId.value() );
  REQUIRE( report.has_value() );
  CHECK( report.value().verdict == ReplayDeviationReport::Verdict::Deviated );
  bool modelDomainPinned = false;
  for ( const auto &entry : report.value().deviationDetails )
  {
    if ( entry.dimension == QStringLiteral( "model" ) )
    {
      CHECK( entry.domain == ReplayDeviationDomain::Model );
      CHECK( entry.identityPin );
      CHECK_FALSE( entry.remediation.isEmpty() );
      modelDomainPinned = true;
    }
  }
  CHECK( modelDomainPinned );
}
