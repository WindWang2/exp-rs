// tests/test_model_runtime_bench.cpp — Platform 4.0 model-runtime benchmark.
//
// Env-gated (SICNU_MODEL_BENCH=1): measures cold/warm session load, tiled
// throughput (tiles/s, pixels/s), peak RSS during a run and cancel latency
// against the committed identity ONNX fixture, then writes the baseline JSON
// (benchmarks/model-runtime-4.json schema, aligned with execution-bench/1).
// Skipped silently when the gate is off — CI runs stay fast; the benchmark
// is a deliberate local action (scripts/run_perf_baseline.sh style).
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal_priv.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#ifdef SICNU_HAS_OPENCV
#include <opencv2/dnn.hpp>
#endif

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::TileInferenceEngine;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

ModelInfo identityCatalogModel( const QTemporaryDir &dir )
{
  const QString name = QStringLiteral( "bench-identity" );
  QDir( dir.path() ).mkpath( name );
  QFile::copy( identityModelPath(), dir.filePath( name + QStringLiteral( "/model.onnx" ) ) );
  QFile manifest( dir.filePath( name + QStringLiteral( "/model.json" ) ) );
  REQUIRE( manifest.open( QIODevice::WriteOnly ) );
  manifest.write( QString( R"({
      "name": "%1",
      "id": "bench/identity",
      "model_version": "1.0",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": "model.onnx" },
      "tiling": { "tile_size": 64, "overlap": 16 }
  })" )
                    .arg( name )
                    .toUtf8() );
  manifest.close();
  ModelCatalog::instance().setDirectory( dir.path().toStdString() );
  const auto model = ModelCatalog::instance().find( "bench/identity" );
  REQUIRE( model.has_value() );
  REQUIRE( model->readiness == ModelReadiness::Ready );
  return *model;
}

std::uint64_t peakRssMb()
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters{};
  GetProcessMemoryInfo( GetCurrentProcess(), &counters, sizeof( counters ) );
  return static_cast<std::uint64_t>( counters.PeakWorkingSetSize ) / ( 1024ULL * 1024ULL );
#else
  return 0; // RSS sampling is Windows-pinned here; Linux baselines can use /proc/self/status
#endif
}

} // namespace

TEST_CASE( "model runtime benchmark (SICNU_MODEL_BENCH=1)", "[.] [model_bench]" )
{
#ifdef SICNU_HAS_OPENCV
  if ( !qEnvironmentVariableIsSet( "SICNU_MODEL_BENCH" ) )
    FAIL( "benchmark gated: set SICNU_MODEL_BENCH=1 to run" );

  QTemporaryDir dir;
  const ModelInfo model = identityCatalogModel( dir );
  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  registry.setMaxCachedSessions( 4 );

  // --- Cold vs warm session load ------------------------------------------
  const auto coldStart = std::chrono::steady_clock::now();
  ModelRuntimePtr session = registry.acquire( model );
  REQUIRE( session );
  const double coldLoadMs =
    std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - coldStart )
      .count();
  session.reset();
  registry.releaseAll();

  const auto warmStart = std::chrono::steady_clock::now();
  session = registry.acquire( model );
  REQUIRE( session );
  const double warmLoadMs =
    std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - warmStart )
      .count();
  CHECK( warmLoadMs <= coldLoadMs + 50.0 ); // the cached acquire cannot be slower by much

  // --- Tiled throughput -----------------------------------------------------
  const QString input = dir.filePath( QStringLiteral( "bench-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 512, 512, 4, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "bench-out.tif" ) );

  RSOperatorContext context;
  TileInferenceEngine engine( model, session );
  const auto runStart = std::chrono::steady_clock::now();
  const auto stats = engine.run( input.toStdString(), {}, output.toStdString(), context );
  const double runMs =
    std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - runStart )
      .count();
  const double tilesPerSec =
    runMs > 0 ? stats.tilesProcessed * 1000.0 / runMs : 0.0;
  const double pixelsPerSec = tilesPerSec * stats.tileSize * stats.tileSize;

  // --- Cancel latency -------------------------------------------------------
  QTemporaryDir cancelDir;
  std::atomic<bool> cancelFlag{ false };
  cancelFlag.store( true ); // armed pre-run: the first check point throws
  RSOperatorContext cancelContext;
  cancelContext.setCancelFlag( &cancelFlag );
  TileInferenceEngine cancelEngine( model, session );
  cancelContext.throwIfCancelled();
  const auto cancelStart = std::chrono::steady_clock::now();
  bool canceled = false;
  try
  {
    cancelEngine.run( input.toStdString(), {},
                    cancelDir.filePath( QStringLiteral( "cancel.tif" ) ).toStdString(),
                    cancelContext );
  }
  catch ( const sicnu::operators::RSOperatorError & )
  {
    canceled = true;
  }
  const double cancelLatencyMs =
    std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - cancelStart )
      .count();
  CHECK( canceled );
  CHECK( cancelLatencyMs < 1000.0 );

  // --- Baseline JSON --------------------------------------------------------
  QJsonObject bench;
  bench["schema"] = QStringLiteral( "model-runtime-bench/1" );
  bench["date"] = QDateTime::currentDateTime().toString( Qt::ISODate );
  QJsonObject hw;
  hw["cuda"] = ModelHardwareCapabilities::detect().cudaAvailable;
  bench["hardware"] = hw;
  bench["cold_load_ms"] = coldLoadMs;
  bench["warm_load_ms"] = warmLoadMs;
  bench["tiles"] = stats.tilesProcessed;
  bench["tile_size"] = stats.tileSize;
  bench["run_ms"] = runMs;
  bench["tiles_per_sec"] = tilesPerSec;
  bench["pixels_per_sec"] = pixelsPerSec;
  bench["peak_rss_mb"] = static_cast<qint64>( peakRssMb() );
  bench["cancel_latency_ms"] = cancelLatencyMs;

  const QString outPath = QStringLiteral( "benchmarks/model-runtime-4.json" );
  QFile outFile( outPath );
  if ( outFile.open( QIODevice::WriteOnly ) )
  {
    outFile.write( QJsonDocument( bench ).toJson( QJsonDocument::Indented ) );
    outFile.close();
    WARN( "benchmark baseline written to benchmarks/model-runtime-4.json" );
  }
  registry.releaseAll();
#else
  FAIL( "benchmark gated: build without OpenCV" );
#endif
}
