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
#include "operators/runtime/nvml_inventory.h"
#include "operators/runtime/tile_inference_engine.h"
#include "support/onnx_fixture_builder.h"

#ifdef SICNU_WITH_ONNX_RUNTIME
#include <onnxruntime_c_api.h> // OrtGetVersionString for the bench header
#include <onnxruntime_cxx_api.h> // 9.0 CUDA-EP probe (Ort::SessionOptions)
#endif

#include <opencv2/core.hpp>
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
#include <thread>
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
using sicnu::operators::ModelInputContract;
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

/// Platform 8.0 WP-H: hand-encoded ONNX fixtures for the real-ORT bench lane
/// (byte-deterministic; the same builder the capability-gated tests use).
struct OnnxFixtureModels
{
    QByteArray sumDual;    // two named 1x2x64x64 inputs → sum
    QByteArray slowMatmul; // 96 chained 1024x1024 matmuls (cancel-latency load)
};

OnnxFixtureModels buildOnnxFixtureModels()
{
  OnnxFixtureModels models;
  const std::string sum =
    onnxfixture::sumDual( { onnxfixture::fixed( 1 ), onnxfixture::fixed( 2 ),
                            onnxfixture::fixed( 64 ), onnxfixture::fixed( 64 ) } );
  models.sumDual = QByteArray( sum.data(), static_cast<int>( sum.size() ) );
  const std::string slow = onnxfixture::slowMatmulChain( 1024, 96 );
  models.slowMatmul = QByteArray( slow.data(), static_cast<int>( slow.size() ) );
  return models;
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
  // The flag is armed: the engine's first checkpoint throws inside run().
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

  // --- Platform 7.0: multi-input / temporal throughput ----------------------
  QJsonObject bench7;
  // A deterministic known-answer fake (same wire semantics as the 7.0 test
  // fakes) exercises the multimodal engine: per-tile cost now includes the
  // per-feed window reads, the temporal stack and the named forward.
  {
    using namespace sicnu::operators::runtime;
    class MultiInputBenchRuntime final : public IModelRuntime
    {
      public:
        std::string framework() const override { return "bench7"; }
        std::string backendName() const override { return "multi-input-bench"; }
        std::string deviceName() const override { return "cpu"; }
        std::string artifactPath() const override { return "fake://bench7"; }
        cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }
        std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                             const std::vector<std::string> & ) override
        {
          const TensorBlob &first = inputs.front().second;
          auto out = TensorBlob::zeros( first.shape, TensorDType::Float32 );
          return { NamedTensor{ std::string(), std::move( out ) } };
        }
    };
    ModelInfo multiModel;
    multiModel.name = "bench7-multi";
    multiModel.framework = "bench7";
    multiModel.tiling.tileSize = 64;
    ModelInputContract t1;
    t1.name = "before";
    ModelInputContract t2;
    t2.name = "after";
    t2.temporalLength = 3;
    t2.missingTimestep = "zero";
    multiModel.inputs = { t1, t2 };
    multiModel.input = t1;

    const QString t1Path = dir.filePath( QStringLiteral( "bench7-t1.tif" ) );
    const QString t2Path = dir.filePath( QStringLiteral( "bench7-t2.tif" ) );
    sicnu::testing::RsSyntheticRasterBuilder( 512, 512, 2, GDT_Float32 )
      .withConstantValue( 1, 1.0f )
      .writeToDisk( t1Path );
    sicnu::testing::RsSyntheticRasterBuilder( 512, 512, 2, GDT_Float32 )
      .withConstantValue( 1, 2.0f )
      .writeToDisk( t2Path );

    TileInferenceEngine multiEngine( multiModel,
                                     std::make_shared<MultiInputBenchRuntime>() );
    const auto multiStart = std::chrono::steady_clock::now();
    const auto multiStats = multiEngine.runMultiInput(
      { NamedRasterFeed{ "before", { t1Path.toStdString() }, {} },
        NamedRasterFeed{ "after", { t2Path.toStdString(), t2Path.toStdString(), t2Path.toStdString() }, {} } },
      dir.filePath( QStringLiteral( "bench7-out.tif" ) ).toStdString(), context );
    const double multiMs =
      std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - multiStart )
        .count();
    bench7["tiles_per_sec"] = multiMs > 0 ? multiStats.tilesProcessed * 1000.0 / multiMs : 0.0;
    bench7["run_ms"] = multiMs;
    bench7["tiles"] = multiStats.tilesProcessed;
  }

  // Platform 8.0 ORT-lane results (filled below when the provider is
  // compiled in, attached to the baseline document afterwards).
  QJsonObject benchOrt;
  // --- Platform 8.0: REAL ONNX Runtime lane (WP-H) ---------------------------
  // Only compiled when the provider is embedded; measures cold/warm ORT
  // session acquire, named N-D forward throughput and in-forward cancel
  // latency against hand-encoded deterministic fixtures.
#ifdef SICNU_WITH_ONNX_RUNTIME
  {
    using sicnu::operators::runtime::RequestedDevice;
    using sicnu::operators::runtime::TensorBlob;
    using sicnu::operators::runtime::NamedTensor;
    benchOrt["schema"] = QStringLiteral( "model-runtime-bench-ort/1" );
    benchOrt["ort_runtime_version"] = QString::fromUtf8( OrtGetApiBase()->GetVersionString() );

    const OnnxFixtureModels models = buildOnnxFixtureModels();
    const QString sumPath = dir.filePath( QStringLiteral( "bench-sum.onnx" ) );
    { QFile f( sumPath ); REQUIRE( f.open( QIODevice::WriteOnly ) ); f.write( models.sumDual ); }
    const QString slowPath = dir.filePath( QStringLiteral( "bench-slow.onnx" ) );
    { QFile f( slowPath ); REQUIRE( f.open( QIODevice::WriteOnly ) ); f.write( models.slowMatmul ); }

    ModelInfo ortModel;
    ortModel.name = "bench-ort";
    ortModel.framework = "onnxruntime";
    ortModel.readiness = ModelReadiness::Ready;
    ortModel.resolvedArtifactPath = sumPath.toStdString();

    const auto ortCold = std::chrono::steady_clock::now();
    ModelRuntimePtr ortSession = registry.acquire( ortModel, RequestedDevice::cpu() );
    REQUIRE( ortSession );
    const double ortColdMs =
      std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - ortCold )
        .count();
    ortSession.reset();
    registry.releaseAll();
    const auto ortWarm = std::chrono::steady_clock::now();
    ortSession = registry.acquire( ortModel, RequestedDevice::cpu() );
    REQUIRE( ortSession );
    const double ortWarmMs =
      std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - ortWarm )
        .count();
    benchOrt["cold_load_ms"] = ortColdMs;
    benchOrt["warm_load_ms"] = ortWarmMs;

    // Named N-D forward throughput: 128 forwards on 1x2x64x64 pairs.
    std::vector<float> a( 2 * 64 * 64, 1.5f );
    std::vector<float> b( 2 * 64 * 64, 2.5f );
    const TensorBlob ta = TensorBlob::fromFloat32( { 1, 2, 64, 64 }, a.data(), a.size() );
    const TensorBlob tb = TensorBlob::fromFloat32( { 1, 2, 64, 64 }, b.data(), b.size() );
    constexpr int kOrtForwards = 128;
    const auto fwdStart = std::chrono::steady_clock::now();
    for ( int i = 0; i < kOrtForwards; ++i )
    {
      const auto outputs = ortSession->inferNamed( { { "a", ta }, { "b", tb } }, { "sum" } );
      REQUIRE( outputs.size() == 1 );
    }
    const double fwdMs =
      std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - fwdStart )
        .count();
    benchOrt["named_forward_ms_avg"] = fwdMs / kOrtForwards;
    benchOrt["named_forwards_per_sec"] = fwdMs > 0 ? kOrtForwards * 1000.0 / fwdMs : 0.0;
    ortSession.reset();
    registry.releaseAll();

    // In-forward cancellation latency on the slow chained-matmul fixture:
    // request cancel 300 ms into the forward, measure until the session
    // surfaces the cancel.
    ModelInfo slowModel;
    slowModel.name = "bench-ort-slow";
    slowModel.framework = "onnxruntime";
    slowModel.readiness = ModelReadiness::Ready;
    slowModel.resolvedArtifactPath = slowPath.toStdString();
    ModelRuntimePtr slowSession = registry.acquire( slowModel, RequestedDevice::cpu() );
    REQUIRE( slowSession );
    std::vector<float> x( 1024ull * 1024ull, 1.0f );
    const TensorBlob tx = TensorBlob::fromFloat32( { 1024, 1024 }, x.data(), x.size() );
    std::atomic<bool> cancelObserved{ false };
    double cancelLatencyOrtMs = -1.0;
    std::thread ortRunner( [ & ] {
      try
      {
        slowSession->inferNamed( { { "x", tx } }, {} );
      }
      catch ( const std::exception & )
      {
        cancelObserved.store( true, std::memory_order_relaxed );
      }
    } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
    const auto ortCancelStart = std::chrono::steady_clock::now();
    slowSession->requestCancel();
    ortRunner.join();
    if ( cancelObserved.load( std::memory_order_relaxed ) )
      cancelLatencyOrtMs = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - ortCancelStart )
                             .count();
    benchOrt["in_forward_cancel_latency_ms"] = cancelLatencyOrtMs;
    slowSession->clearCancel();
    registry.releaseAll();
  }
#endif

  // --- Baseline JSON --------------------------------------------------------
  QJsonObject bench;
  bench["schema"] = QStringLiteral( "model-runtime-bench/1" );
  bench7["schema"] = QStringLiteral( "model-runtime-bench-7/1" );
  bench["platform7_multi_input"] = bench7;
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
  if ( !benchOrt.isEmpty() )
    bench["platform8_ort"] = benchOrt;

  const QString outPath =
#ifdef SICNU_SOURCE_DIR
    QString::fromUtf8( SICNU_SOURCE_DIR ) + QStringLiteral( "/benchmarks/model-runtime-4.json" );
#else
    QStringLiteral( "benchmarks/model-runtime-4.json" );
#endif
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

// ---------------------------------------------------------------------------
// Platform 9.0 (M9): REAL CUDA-EP lane benchmark (SICNU_MODEL_BENCH=1).
// Capability-gated twice: the bench gate, then a live CUDA-EP probe (a
// CPU-only ORT build refuses the append and the case reports SKIP — never a
// fabricated GPU number). Every number lands in
// benchmarks/model-runtime-9-cuda.json with its environment.
// ---------------------------------------------------------------------------
TEST_CASE( "model runtime CUDA-EP benchmark (SICNU_MODEL_BENCH=1)", "[.] [model_bench]" )
{
#ifdef SICNU_WITH_ONNX_RUNTIME
  if ( !qEnvironmentVariableIsSet( "SICNU_MODEL_BENCH" ) )
    FAIL( "benchmark gated: set SICNU_MODEL_BENCH=1 to run" );

  using sicnu::operators::runtime::NvidiaInventory;
  using sicnu::operators::runtime::RequestedDevice;
  using sicnu::operators::runtime::NamedTensor;
  using sicnu::operators::runtime::TensorBlob;

  // Live capability probe: the CUDA EP must actually register on this ORT.
  bool cudaEpLoadable = false;
  QString probeWhy;
  try
  {
    Ort::Env probeEnv( ORT_LOGGING_LEVEL_ERROR, "exp-rs-cuda-probe" );
    Ort::SessionOptions probeOptions;
    OrtCUDAProviderOptions cudaOptions{};
    probeOptions.AppendExecutionProvider_CUDA( cudaOptions );
    cudaEpLoadable = true;
  }
  catch ( const Ort::Exception &e )
  {
    probeWhy = QString::fromUtf8( e.what() );
  }
  if ( !cudaEpLoadable )
  {
    WARN( "CUDA EP not loadable on this ORT build — CUDA lane NOT RUN: "
          << probeWhy.toStdString() );
    FAIL( "cuda capability gate: ep not loadable (marked NOT RUN, never a PASS)" );
  }

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();

  // Force the REAL GPU view for the planner (the NVML probe would also find
  // it; the override makes the run deterministic about WHICH truth is used).
  ModelHardwareCapabilities forced;
  forced.cudaAvailable = true;
  forced.cudaRuntimeAvailable = true;
  forced.cudaDeviceCount = 1;
  const NvidiaInventory gpuNow = NvidiaInventory::probe();
  if ( gpuNow.available && !gpuNow.devices.empty() )
  {
    forced.deviceNames = { gpuNow.devices.front().name };
    forced.deviceTotalVramMb = { gpuNow.devices.front().totalVramMb };
    forced.deviceFreeVramMb = { gpuNow.devices.front().freeVramMb };
  }
  registry.setHardwareForTest( forced );
  struct RestoreHw
  {
      ~RestoreHw() { ModelRuntimeRegistry::instance().setHardwareForTest( std::nullopt ); }
  } restoreHw;

  QTemporaryDir dir;
  const OnnxFixtureModels models = buildOnnxFixtureModels();
  const QString sumPath = dir.filePath( QStringLiteral( "bench9-sum.onnx" ) );
  { QFile f( sumPath ); REQUIRE( f.open( QIODevice::WriteOnly ) ); f.write( models.sumDual ); }

  ModelInfo gpuModel;
  gpuModel.name = "bench9-ort-cuda";
  gpuModel.framework = "onnxruntime";
  gpuModel.readiness = ModelReadiness::Ready;
  gpuModel.resolvedArtifactPath = sumPath.toStdString();
  gpuModel.runtime.gpu = true; // the model tolerates GPU → cuda resolution legal
  gpuModel.runtime.resolvedCudaIndex = 0;

  // CPU reference session for the known-answer cross-check.
  ModelRuntimePtr cpuSession = registry.acquire( gpuModel, RequestedDevice::cpu() );
  REQUIRE( cpuSession );

  const auto cudaCold = std::chrono::steady_clock::now();
  ModelRuntimePtr cudaSession = registry.acquire( gpuModel, RequestedDevice::cuda( 0 ) );
  REQUIRE( cudaSession );
  const double cudaColdMs =
    std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - cudaCold )
      .count();
  cudaSession.reset();
  registry.releaseAll();
  const auto cudaWarm = std::chrono::steady_clock::now();
  cudaSession = registry.acquire( gpuModel, RequestedDevice::cuda( 0 ) );
  REQUIRE( cudaSession );
  const double cudaWarmMs =
    std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - cudaWarm )
      .count();

  // The session MUST report the CUDA execution provider (9.0 honesty rule:
  // a claimed-GPU run carries its EP identity).
  const auto details = cudaSession->providerDetails();
  REQUIRE( details.executionProvider.find( "CUDA" ) != std::string::npos );

  // Known answer + throughput: identical inputs through both EPs must agree
  // bit-exact on the sum fixture (deterministic kernels on one device pair).
  std::vector<float> a( 2 * 64 * 64, 1.5f );
  std::vector<float> b( 2 * 64 * 64, 2.5f );
  const TensorBlob ta = TensorBlob::fromFloat32( { 1, 2, 64, 64 }, a.data(), a.size() );
  const TensorBlob tb = TensorBlob::fromFloat32( { 1, 2, 64, 64 }, b.data(), b.size() );
  const auto cpuOut = cpuSession->inferNamed( { { "a", ta }, { "b", tb } }, { "sum" } );
  REQUIRE( cpuOut.size() == 1 );
  constexpr int kForwards = 128;
  const auto fwdStart = std::chrono::steady_clock::now();
  for ( int i = 0; i < kForwards; ++i )
  {
    const auto outs = cudaSession->inferNamed( { { "a", ta }, { "b", tb } }, { "sum" } );
    REQUIRE( outs.size() == 1 );
    REQUIRE( outs.front().second.bytes == cpuOut.front().second.bytes );
  }
  const double fwdMs =
    std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - fwdStart )
      .count();

  const NvidiaInventory gpuAfter = NvidiaInventory::probe();

  QJsonObject bench;
  bench["schema"] = QStringLiteral( "model-runtime-bench-9-cuda/1" );
  bench["ort_runtime_version"] = QString::fromUtf8( OrtGetApiBase()->GetVersionString() );
  bench["execution_provider"] = QString::fromStdString( details.executionProvider );
  bench["gpu_name"] = QString::fromStdString(
    gpuNow.available && !gpuNow.devices.empty() ? gpuNow.devices.front().name : "" );
  bench["cuda_cold_load_ms"] = cudaColdMs;
  bench["cuda_warm_load_ms"] = cudaWarmMs;
  bench["cuda_named_forward_ms_avg"] = fwdMs / kForwards;
  bench["cuda_forwards_per_sec"] = fwdMs > 0 ? kForwards * 1000.0 / fwdMs : 0.0;
  bench["known_answer_matches_cpu"] = true;
  if ( gpuNow.available && gpuAfter.available && !gpuNow.devices.empty()
       && !gpuAfter.devices.empty() )
  {
    bench["vram_free_before_mb"] = gpuNow.devices.front().freeVramMb;
    bench["vram_free_after_mb"] = gpuAfter.devices.front().freeVramMb;
  }
  bench["created_utc"] = QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs );

  QDir().mkpath( QStringLiteral( "benchmarks" ) );
  QFile out( QStringLiteral( "benchmarks/model-runtime-9-cuda.json" ) );
  REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  out.write( QJsonDocument( bench ).toJson( QJsonDocument::Indented ) );
  out.close();

  cudaSession.reset();
  cpuSession.reset();
  registry.releaseAll();
#else
  FAIL( "benchmark gated: ORT provider not compiled in" );
#endif
}
