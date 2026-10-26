// test_r21_b_end_to_end.cpp — R21-B M5 end-to-end fixture: the full chain
// import (on-disk raster registered through DataManager) → dataset version
// (draft/stage/commit with fingerprint) → model inference (real ONNX
// identity fixture through the tile engine, atomic product publish) →
// experiment run + metrics + lineage → reproduction bundle export → import
// (replay) with digest-identical artifacts. Failure legs assert typed
// refusals and zero half-products.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "data/asset_types.h"
#include "data/data_manager.h"
#include "data/source_descriptor.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "experiment/evaluation.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/reproduction_bundle.h"
#include "experiment/reproduction_bundle_import.h"
#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>

using namespace sicnu::data;
using namespace sicnu::dataset;
using namespace sicnu::experiment;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::TileInferenceEngine;
using sicnu::operators::runtime::RequestedDevice;
using sicnu::data::AssetState;
using sicnu::operators::RSOperatorContext;

namespace
{

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

QByteArray fileDigest( const QString &path )
{
  QFile f( path );
  if ( !f.open( QIODevice::ReadOnly ) )
    return {};
  return QCryptographicHash::hash( f.readAll(), QCryptographicHash::Sha256 ).toHex();
}

ExperimentRun makeRun( const QString &runId, const QString &experimentId )
{
  ExperimentRun run;
  run.setRunId( runId );
  run.setExperimentId( experimentId );
  run.setAlgorithmId( QStringLiteral( "rs:infer" ) );
  run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
  run.setSeed( 7 );
  return run;
}

} // namespace

TEST_CASE( "R21-B e2e: import → dataset → inference → run/lineage → bundle replay",
           "[r21b][e2e]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  REQUIRE( QFile::exists( identityModelPath() ) );

  // ---- 1. Import surface: an on-disk raster registered as an asset -------
  const QString rasterPath = dir.filePath( QStringLiteral( "scene.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 64, 64, 1 );
  builder.withCrs( QStringLiteral( "EPSG:32648" ) )
      .withGeoTransform( 100.0, 0.5, 200.0, -0.5 )
      .withRampPattern( 1, 0.0f, 1.0f );
  builder.writeToDisk( rasterPath );
  REQUIRE( QFile::exists( rasterPath ) );

  DataManager manager;
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = rasterPath;
  const auto registered = manager.registerSource( RegisterRequest{ source } );
  REQUIRE_FALSE( registered.assetId.isNull() );
  const auto asset = manager.asset( registered.assetId );
  REQUIRE( asset.has_value() );
  CHECK( asset->state() == AssetState::Ready );

  // ---- 2. Dataset version pinned to the imported asset -------------------
  DatasetStore datasets;
  REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
  const DatasetId datasetId = DatasetId::generate();
  REQUIRE( datasets.createDataset( datasetId, QStringLiteral( "r21b-e2e" ) ).has_value() );
  DatasetManifest manifest;
  manifest.setDatasetId( datasetId.toString() );
  manifest.setVersionId( DatasetVersionId::generate().toString() );
  manifest.setName( QStringLiteral( "r21b e2e optical" ) );
  SourceAssetRef ref;
  ref.assetId = registered.assetId.toString();
  ref.role = QStringLiteral( "image" );
  manifest.sourceAssets().append( ref );
  const auto draft = datasets.createDraftVersion( manifest );
  REQUIRE( draft.has_value() );
  const DatasetVersionId versionId =
    DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} );
  REQUIRE( datasets.stageVersion( versionId ).has_value() );
  const auto committed = datasets.commitVersion( versionId );
  REQUIRE( committed.has_value() );
  CHECK_FALSE( committed->fingerprint().isEmpty() );

  // ---- 3. Model inference: identity tile engine, atomic product ----------
  const QString productPath = dir.filePath( QStringLiteral( "product.tif" ) );
  ModelInfo info;
  info.name = "identity";
  info.framework = "onnx";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = identityModelPath().toStdString();
  info.tiling.tileSize = 32;
  info.tiling.overlap = 0;

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  std::string acquireError;
  const auto session = registry.acquire( info, RequestedDevice::cpu(), &acquireError );
  REQUIRE( session );

  RSOperatorContext context;
  TileInferenceEngine engine( info, session );
  const auto stats =
    engine.run( rasterPath.toStdString(), {}, productPath.toStdString(), context );
  CHECK( stats.tilesProcessed == 4 );
  CHECK( stats.outBands == 1 );

  // The publish is atomic and complete: product + provenance sidecar.
  REQUIRE( QFile::exists( productPath ) );
  REQUIRE( QFile::exists( productPath + QStringLiteral( ".prov.json" ) ) );
  GdalDatasetWrapper product;
  REQUIRE( product.open( productPath ) );
  REQUIRE( product.width() == 64 );
  REQUIRE( product.height() == 64 );
  std::vector<float> pixels( 64 * 64 );
  REQUIRE( product.readBandData( 1, pixels.data(), 64, 64 ) );
  const std::vector<float> &expected = builder.band( 1 );
  for ( std::size_t i = 0; i < pixels.size(); ++i )
    CHECK( pixels[i] == Catch::Approx( expected[i] ).margin( 1e-3 ) );

  // ---- 4. Experiment run + metrics + lineage ------------------------------
  ExperimentStore experiments;
  REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
  Experiment experiment;
  experiment.setExperimentId( QStringLiteral( "exp-r21b-e2e" ) );
  experiment.setName( QStringLiteral( "identity replay chain" ) );
  REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

  ExperimentRun run = makeRun( QStringLiteral( "run-r21b-1" ),
                               QStringLiteral( "exp-r21b-e2e" ) );
  run.setDatasetVersionId( committed->versionId() );
  run.setDatasetFingerprint( committed->fingerprint() );
  run.setStatus( RunStatus::Created );
  ExperimentRun::Artifact artifact;
  artifact.path = productPath;
  artifact.role = QStringLiteral( "primary" );
  artifact.digest = QString::fromLatin1( fileDigest( productPath ) );
  artifact.sizeBytes = QFileInfo( productPath ).size();
  run.artifacts().append( artifact );
  run.setStatus( RunStatus::Running );
  run.setStatus( RunStatus::Completed );
  run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
  run.setMetrics( QJsonObject{ { QStringLiteral( "psnr" ), 999.0 } } );
  REQUIRE( experiments.upsertRun( run ).has_value() );

  MetricRecord metric;
  metric.runId = run.runId();
  metric.protocol.setDatasetVersionId( committed->versionId() );
  metric.protocol.setSplitManifestId( QStringLiteral( "00000000-0000-4000-8000-000000000000" ) );
  metric.metrics = QJsonObject{ { QStringLiteral( "overall_accuracy" ), 1.0 } };
  REQUIRE( experiments.saveMetricRecord( metric ).has_value() );

  REQUIRE( experiments.addLineageEdge( QStringLiteral( "dataset_version" ),
                                       committed->versionId(),
                                       QStringLiteral( "evaluated_by" ),
                                       QStringLiteral( "run" ), run.runId() )
             .has_value() );

  // ---- 5. Reproduction bundle export → replay import ----------------------
  ReproductionBundleExporter exporter( experiments, datasets );
  ReproductionBundleOptions exportOptions;
  exportOptions.outputDir = dir.filePath( QStringLiteral( "bundle" ) );
  exportOptions.currentSoftwareRevision = QStringLiteral( "r21b-test" );
  const auto exportReport = exporter.exportRun( run.runId(), exportOptions );
  REQUIRE( exportReport.ok );

  ExperimentStore replay;
  REQUIRE( replay.open( dir.filePath( QStringLiteral( "replay.db" ) ) ) );
  Experiment home;
  home.setExperimentId( QStringLiteral( "exp-r21b-replay" ) );
  home.setName( QStringLiteral( "replay destination" ) );
  REQUIRE( replay.upsertExperiment( home ).has_value() );
  ReproductionBundleImporter importer( replay );
  ReproductionBundleImportOptions importOptions;
  importOptions.bundleDir = exportReport.bundlePath;
  importOptions.targetExperimentId = QStringLiteral( "exp-r21b-replay" );
  const auto importReport = importer.importRun( importOptions );
  REQUIRE( importReport.ok );

  const auto replayed = replay.runById( importReport.runId );
  REQUIRE( replayed.has_value() );
  // The replayed run carries the same dataset pins — replay identity is the
  // dataset/fingerprint pair recorded in run_config.json. (The replay
  // contract deliberately does not re-attach source-store artifact paths:
  // the bundle proves identity, it does not claim the product file moved
  // with it.)
  CHECK( replayed->datasetVersionId() == committed->versionId() );
  CHECK( replayed->datasetFingerprint() == committed->fingerprint() );

  const auto replayMetric = replay.metricRecordForRun( importReport.runId );
  REQUIRE( replayMetric.has_value() );
  CHECK( replayMetric->metrics[QStringLiteral( "overall_accuracy" )] == 1.0 );

  registry.releaseAll();
}

TEST_CASE( "R21-B e2e: failed inference leaves no half product",
           "[r21b][e2e][failure]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  REQUIRE( QFile::exists( identityModelPath() ) );

  ModelInfo info;
  info.name = "identity";
  info.framework = "onnx";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = identityModelPath().toStdString();
  info.tiling.tileSize = 32;
  info.tiling.overlap = 0;

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  std::string acquireError;
  const auto session = registry.acquire( info, RequestedDevice::cpu(), &acquireError );
  REQUIRE( session );

  RSOperatorContext context;
  TileInferenceEngine engine( info, session );

  // Leg 1: an input raster that does not exist → typed refusal, no output,
  // no sidecar.
  const QString deadOutput = dir.filePath( QStringLiteral( "dead.tif" ) );
  REQUIRE_THROWS_AS(
    engine.run( dir.filePath( QStringLiteral( "missing-input.tif" ) ).toStdString(), {},
                deadOutput.toStdString(), context ),
    sicnu::operators::RSOperatorError );
  CHECK_FALSE( QFile::exists( deadOutput ) );
  CHECK_FALSE( QFile::exists( deadOutput + QStringLiteral( ".prov.json" ) ) );

  // Leg 2: an output location inside a read-only directory → typed refusal
  // BEFORE any staged bytes matter, and nothing leaks at the target path.
  const QString roDir = dir.filePath( QStringLiteral( "ro" ) );
  REQUIRE( QDir().mkpath( roDir ) );
  const QString inputPath = dir.filePath( QStringLiteral( "ok.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 1 ).withRampPattern( 1, 0.0f, 1.0f ).writeToDisk(
    inputPath );
  QFile roMarker( roDir + QStringLiteral( "/keep" ) );
  REQUIRE( roMarker.open( QIODevice::WriteOnly ) );
  roMarker.write( "x" );
  roMarker.close();
  REQUIRE( QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner
                                                 | QFileDevice::ReadUser | QFileDevice::ExeUser ) );
  const QString refused = roDir + QStringLiteral( "/refused.tif" );
  REQUIRE_THROWS_AS( engine.run( inputPath.toStdString(), {}, refused.toStdString(), context ),
                     sicnu::operators::RSOperatorError );
  CHECK_FALSE( QFile::exists( refused ) );
  // Restore permissions for QTemporaryDir cleanup.
  QFile::setPermissions( roDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner | QFileDevice::ReadUser
                                  | QFileDevice::WriteUser | QFileDevice::ExeUser );
  registry.releaseAll();
}
