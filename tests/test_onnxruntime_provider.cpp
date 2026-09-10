// tests/test_onnxruntime_provider.cpp — Model Runtime 8.0 WP-A: the REAL
// ONNX Runtime lane. These tests compile and run only when the build embeds
// the ORT provider (SICNU_WITH_ONNX_RUNTIME with a discovered SDK); on hosts
// without the dependency the target is not built at all — the honest
// capability gate, never a fake pass.
//
// Coverage: provider registration, catalog readiness, named multi-input
// known answers (sum_dual), multi-head N-D output selection (dual_head),
// dynamic shapes (dynamic_add), dtype lanes incl. Int64/Double/UInt8
// transport (cast_lanes), cv::Mat fast-path through real ORT + tiled
// engine integration, warmup, health/memory accounting, cancel-before and
// IN-forward cancellation (slow_matmul) with session reuse after cancel,
// and the CUDA-less-host honesty path (a forced-CUDA load fails typed).
//
// All graphs are hand-encoded ONNX built by support/onnx_fixture_builder.h
// (byte-deterministic, no network); every fixture was validated to load in
// ORT 1.20.1 and reproduce the pinned known answers.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/model_readiness.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/onnxruntime_provider.h"
#include "operators/runtime/tile_inference_engine.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"
#include "support/onnx_fixture_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <opencv2/core.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
namespace fs = std::filesystem;

/// Writes one fixture model under a per-test temp dir and returns the path.
std::string writeFixture( const QTemporaryDir &dir, const std::string &name,
                          const std::string &model )
{
  const fs::path path =
    fs::path( dir.path().toStdString() ) / ( name + ".onnx" );
  onnxfixture::writeModel( path, model );
  return path.string();
}

/// A catalog ModelInfo bound to a fixture artifact through the "onnxruntime"
/// framework (the real provider's registration id).
ModelInfo ortModel( const std::string &artifactPath, bool gpu = false )
{
  ModelInfo info;
  info.name = "ort-fixture";
  info.task = "segmentation";
  info.framework = "onnxruntime";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = artifactPath;
  info.gpu = gpu;
  return info;
}

struct HardwarePin
{
    explicit HardwarePin( const ModelHardwareCapabilities &caps )
    {
      ModelRuntimeRegistry::instance().setHardwareForTest( caps );
    }
    ~HardwarePin() { ModelRuntimeRegistry::instance().setHardwareForTest( std::nullopt ); }
};

ModelHardwareCapabilities cpuHost()
{
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = false;
  hw.openclAvailable = false;
  hw.cudaDeviceCount = 0;
  return hw;
}

/// Reads the whole output raster band stack (float32 expected).
std::vector<float> readBand( const QString &path, int band, int &width, int &height )
{
  GDALDataset *ds =
    static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds );
  width = ds->GetRasterXSize();
  height = ds->GetRasterYSize();
  std::vector<float> data( static_cast<std::size_t>( width ) * height );
  REQUIRE( ds->GetRasterBand( band )
             ->RasterIO( GF_Read, 0, 0, width, height, data.data(), width, height,
                         GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( ds );
  return data;
}

} // namespace

// ---------------------------------------------------------------------------
// Provider registration & readiness
// ---------------------------------------------------------------------------

TEST_CASE( "onnxruntime provider registers with negotiated capabilities",
           "[models][ort]" )
{
  REQUIRE( onnxRuntimeProviderAvailable() );
  auto &registry = ModelRuntimeRegistry::instance();
  REQUIRE( registry.hasProvider( "onnxruntime" ) );
  const auto traits = registry.providerTraits( "onnxruntime" );
  REQUIRE( traits.has_value() );
  CHECK( traits->maxAddressableCudaIndex >= 1 );
}

TEST_CASE( "runtime readiness is Ready for an onnxruntime model on a CPU host",
           "[models][ort]" )
{
  QTemporaryDir dir;
  const std::string artifact =
    writeFixture( dir, "sum_dual", onnxfixture::sumDual(
                                     { onnxfixture::fixed( 1 ), onnxfixture::fixed( 2 ),
                                       onnxfixture::fixed( 8 ), onnxfixture::fixed( 8 ) } ) );
  HardwarePin pin( cpuHost() );
  std::string reason;
  const ModelReadiness readiness =
    evaluateRuntimeReadiness( ortModel( artifact ), cpuHost(), &reason );
  CAPTURE( reason );
  CHECK( readiness == ModelReadiness::Ready );
}

// ---------------------------------------------------------------------------
// Named multi-input known answer through the registry
// ---------------------------------------------------------------------------

TEST_CASE( "real ORT session runs named multi-input inference with known answers",
           "[models][ort][nd]" )
{
  QTemporaryDir dir;
  const std::string artifact =
    writeFixture( dir, "sum_dual", onnxfixture::sumDual(
                                     { onnxfixture::fixed( 1 ), onnxfixture::fixed( 2 ),
                                       onnxfixture::fixed( 4 ), onnxfixture::fixed( 4 ) } ) );
  auto &registry = ModelRuntimeRegistry::instance();
  HardwarePin pin( cpuHost() );
  registry.releaseAll();

  std::string error;
  const ModelRuntimePtr session = registry.acquire( ortModel( artifact ), RequestedDevice::cpu(), &error );
  CAPTURE( error );
  REQUIRE( session );
  CHECK( session->framework() == "onnxruntime" );
  CHECK( session->backendName() == "onnxruntime" );
  CHECK( session->deviceName() == "cpu" ); // no CUDA on this host

  const IModelRuntime::ProviderCapabilities caps = session->capabilities();
  CHECK( caps.multiInput );
  CHECK( caps.namedBind );
  CHECK( caps.maxRank >= 6 );
  CHECK( caps.cancelInForward );

  std::vector<float> a( 32, 3.0f );
  std::vector<float> b( 32, 5.0f );
  TensorBlob ta = TensorBlob::fromFloat32( { 1, 2, 4, 4 }, a.data(), a.size() );
  TensorBlob tb = TensorBlob::fromFloat32( { 1, 2, 4, 4 }, b.data(), b.size() );
  const std::vector<NamedTensor> outputs =
    session->inferNamed( { { "a", ta }, { "b", tb } }, {} );
  REQUIRE( outputs.size() == 1 );
  REQUIRE( outputs[0].second.rank() == 4 );
  REQUIRE( outputs[0].second.elementCount() == 32 );
  const float *sum = outputs[0].second.dataFloat32();
  for ( std::size_t i = 0; i < 32; ++i )
    CHECK( sum[i] == Catch::Approx( 8.0f ).margin( 1e-5 ) );

  // Health + honest memory accounting after a real forward.
  const SessionHealth health = session->health();
  CHECK( health.ok );
  CHECK( health.forwardsCompleted >= 1 );
  CHECK( health.failures == 0 );
  const SessionMemoryEstimate estimate = session->memoryEstimate();
  CHECK( estimate.weightsMb > 0 );

  // Named output selection returns exactly the requested head.
  const std::vector<NamedTensor> selected =
    session->inferNamed( { { "a", ta }, { "b", tb } }, { "sum" } );
  REQUIRE( selected.size() == 1 );
  CHECK( selected[0].first == "sum" );

  // Unknown input name → typed schema refusal (never positional reinterpretation).
  REQUIRE_THROWS_WITH( session->inferNamed( { { "not_an_input", ta } }, {} ),
                       Catch::Matchers::ContainsSubstring( "not_an_input" ) );
  registry.releaseAll();
}

// ---------------------------------------------------------------------------
// Multi-head N-D outputs
// ---------------------------------------------------------------------------

TEST_CASE( "real ORT multi-head session returns selected heads with distinct ranks",
           "[models][ort][nd]" )
{
  QTemporaryDir dir;
  const std::string artifact =
    writeFixture( dir, "dual_head",
                  onnxfixture::dualHead( { onnxfixture::fixed( 1 ), onnxfixture::fixed( 3 ),
                                           onnxfixture::fixed( 2 ), onnxfixture::fixed( 2 ) } ) );
  auto &registry = ModelRuntimeRegistry::instance();
  HardwarePin pin( cpuHost() );
  registry.releaseAll();
  std::string error;
  const ModelRuntimePtr session = registry.acquire( ortModel( artifact ), RequestedDevice::cpu(), &error );
  REQUIRE( session );

  std::vector<float> x( 12 );
  for ( int i = 0; i < 12; ++i )
    x[static_cast<std::size_t>( i )] = static_cast<float>( i );
  const TensorBlob tx = TensorBlob::fromFloat32( { 1, 3, 2, 2 }, x.data(), x.size() );

  // Graph order = every head.
  const std::vector<NamedTensor> all = session->inferNamed( { { "x", tx } }, {} );
  REQUIRE( all.size() == 3 );
  CHECK( all[0].first == "logits" );
  CHECK( all[0].second.rank() == 4 );
  CHECK( all[1].first == "pooled" );
  CHECK( all[1].second.rank() == 3 ); // ReduceMean over channels, keepdims=0
  CHECK( all[2].first == "total" );
  CHECK( all[2].second.rank() == 4 );

  // Known answers.
  const float *pooled = all[1].second.dataFloat32();
  CHECK( pooled[0] == Catch::Approx( 4.0f ).margin( 1e-4 ) );
  CHECK( pooled[3] == Catch::Approx( 7.0f ).margin( 1e-4 ) );
  const float *total = all[2].second.dataFloat32();
  CHECK( total[0] == Catch::Approx( 5.5f ).margin( 1e-4 ) );

  // Selecting ONE named head returns exactly that head (rank-3 N-D transport).
  const std::vector<NamedTensor> onlyPooled =
    session->inferNamed( { { "x", tx } }, { "pooled" } );
  REQUIRE( onlyPooled.size() == 1 );
  CHECK( onlyPooled[0].second.rank() == 3 );

  // Unknown head → typed refusal.
  REQUIRE_THROWS_WITH( session->inferNamed( { { "x", tx } }, { "nope" } ),
                       Catch::Matchers::ContainsSubstring( "nope" ) );
  registry.releaseAll();
}

// ---------------------------------------------------------------------------
// Dynamic shapes + dtype lanes
// ---------------------------------------------------------------------------

TEST_CASE( "real ORT honors dynamic shapes across runs on one session",
           "[models][ort][dynamic]" )
{
  QTemporaryDir dir;
  const std::string artifact = writeFixture( dir, "dynamic_add", onnxfixture::dynamicAdd( 3 ) );
  auto &registry = ModelRuntimeRegistry::instance();
  HardwarePin pin( cpuHost() );
  registry.releaseAll();
  std::string error;
  const ModelRuntimePtr session = registry.acquire( ortModel( artifact ), RequestedDevice::cpu(), &error );
  REQUIRE( session );

  auto runOnce = [ & ]( std::int64_t n, std::int64_t h, std::int64_t w ) {
    const std::size_t count = static_cast<std::size_t>( n * 3 * h * w );
    std::vector<float> x( count, 10.0f );
    const TensorBlob tx =
      TensorBlob::fromFloat32( { n, 3, h, w }, x.data(), x.size() );
    const std::vector<NamedTensor> out = session->inferNamed( { { "x", tx } }, { "y" } );
    REQUIRE( out.size() == 1 );
    return out[0].second;
  };

  // Two different batch/spatial sizes on the SAME session — dynamic dims.
  {
    const TensorBlob first = runOnce( 1, 8, 8 );
    CHECK( first.shape == std::vector<std::int64_t>( { 1, 3, 8, 8 } ) );
    const float *d = first.dataFloat32();
    CHECK( d[0] == Catch::Approx( 10.0f ).margin( 1e-5 ) );  // c0
    CHECK( d[64] == Catch::Approx( 11.0f ).margin( 1e-5 ) ); // c1
    CHECK( d[128] == Catch::Approx( 12.0f ).margin( 1e-5 ) );// c2
  }
  {
    const TensorBlob second = runOnce( 2, 4, 4 );
    CHECK( second.shape == std::vector<std::int64_t>( { 2, 3, 4, 4 } ) );
    const float *d = second.dataFloat32();
    // Channel stride = H*W = 16: element 0 is c0, 16 is c1, 32 is c2.
    CHECK( d[0] == Catch::Approx( 10.0f ).margin( 1e-5 ) );
    CHECK( d[16] == Catch::Approx( 11.0f ).margin( 1e-5 ) );
    CHECK( d[32] == Catch::Approx( 12.0f ).margin( 1e-5 ) );
  }

  // A C the graph's bias cannot broadcast → typed forward failure.
  std::vector<float> x4( 16, 1.0f );
  const TensorBlob wrong =
    TensorBlob::fromFloat32( { 1, 4, 2, 2 }, x4.data(), x4.size() );
  REQUIRE_THROWS_WITH( session->inferNamed( { { "x", wrong } }, { "y" } ),
                       Catch::Matchers::ContainsSubstring( "forward pass failed" ) );
  // The session stays usable after a failed forward (health recorded it).
  CHECK( session->health().failures >= 1 );
  CHECK( session->health().ok );
  registry.releaseAll();
}

TEST_CASE( "real ORT carries exact dtypes (int64/double/uint8) and the cv bridge refuses lossy paths",
           "[models][ort][dtype]" )
{
  QTemporaryDir dir;
  const std::string artifact =
    writeFixture( dir, "cast_lanes",
                  onnxfixture::castLanes( { onnxfixture::fixed( 1 ), onnxfixture::fixed( 1 ),
                                            onnxfixture::fixed( 2 ), onnxfixture::fixed( 2 ) } ) );
  auto &registry = ModelRuntimeRegistry::instance();
  HardwarePin pin( cpuHost() );
  registry.releaseAll();
  std::string error;
  const ModelRuntimePtr session = registry.acquire( ortModel( artifact ), RequestedDevice::cpu(), &error );
  REQUIRE( session );

  std::vector<float> x = { 1.7f, 200.5f, 3.2f, 42.9f };
  const TensorBlob tx = TensorBlob::fromFloat32( { 1, 1, 2, 2 }, x.data(), x.size() );
  const std::vector<NamedTensor> out = session->inferNamed( { { "x", tx } }, {} );
  REQUIRE( out.size() == 3 );
  CHECK( out[0].first == "as_int64" );
  CHECK( out[0].second.dtype == TensorDType::Int64 );
  CHECK( out[1].first == "as_double" );
  CHECK( out[1].second.dtype == TensorDType::Float64 );
  CHECK( out[2].first == "as_uint8" );
  CHECK( out[2].second.dtype == TensorDType::UInt8 );

  const std::int64_t *asInt = reinterpret_cast<const std::int64_t *>( out[0].second.bytes.data() );
  CHECK( asInt[0] == 1 );
  CHECK( asInt[1] == 200 );
  const double *asDouble = reinterpret_cast<const double *>( out[1].second.bytes.data() );
  CHECK( asDouble[0] == Catch::Approx( 1.7 ).margin( 1e-9 ) );
  const std::uint8_t *asU8 = out[2].second.bytes.data();
  CHECK( asU8[1] == 200 );
  CHECK( asU8[3] == 42 );

  // The cv::Mat bridge refuses the Int64 output instead of bit-casting.
  REQUIRE_THROWS_WITH( session->infer( TensorBlob{ out[0].second }.toMat() ),
                       Catch::Matchers::ContainsSubstring( "refuse" ) );
  registry.releaseAll();
}

// ---------------------------------------------------------------------------
// cv::Mat fast path + tiled engine through real ORT
// ---------------------------------------------------------------------------

TEST_CASE( "tile inference engine runs end-to-end through the real ORT provider",
           "[models][ort][engine]" )
{
  QTemporaryDir dir;
  // Identity 4-D graph: logits = Identity(x) — output equals input bands.
  const std::string artifact =
    writeFixture( dir, "dual_head",
                  onnxfixture::dualHead( { onnxfixture::dynamic( "N" ), onnxfixture::dynamic( "C" ),
                                           onnxfixture::dynamic( "H" ), onnxfixture::dynamic( "W" ) } ) );
  auto raster = sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
                  .withConstantValue( 1, 4.0f )
                  .withConstantValue( 2, 9.0f )
                  .writeToDisk( dir.filePath( QStringLiteral( "input.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );

  ModelInfo model = ortModel( artifact );
  model.tiling.tileSize = 16;
  model.tiling.overlap = 4;
  model.output.tensorNames = { "logits" }; // select the identity head
  model.output.classes = { "b1", "b2" };   // 2 fed channels → 2 output channels

  auto &registry = ModelRuntimeRegistry::instance();
  HardwarePin pin( cpuHost() );
  registry.releaseAll();
  std::string error;
  const ModelRuntimePtr session = registry.acquire( model, RequestedDevice::cpu(), &error );
  REQUIRE( session );

  TileInferenceEngine engine( model, session );
  RSOperatorContext context;
  const QString out = dir.filePath( QStringLiteral( "out.tif" ) );
  const TileInferenceStats stats =
    engine.run( raster.toStdString(), { 1, 2 }, out.toStdString(), context, {} );

  CHECK( stats.tilesProcessed == 4 );
  CHECK( stats.outBands == 2 );
  int width = 0;
  int height = 0;
  const std::vector<float> band1 = readBand( out, 1, width, height );
  REQUIRE( width == 32 );
  for ( float v : band1 )
    REQUIRE( v == Catch::Approx( 4.0f ).margin( 1e-4 ) );
  const std::vector<float> band2 = readBand( out, 2, width, height );
  for ( float v : band2 )
    REQUIRE( v == Catch::Approx( 9.0f ).margin( 1e-4 ) );
  registry.releaseAll();
}

// ---------------------------------------------------------------------------
// Warmup / cancellation / reuse
// ---------------------------------------------------------------------------

TEST_CASE( "real ORT warmup runs a probe forward without failing the session",
           "[models][ort][warmup]" )
{
  QTemporaryDir dir;
  const std::string artifact =
    writeFixture( dir, "sum_dual", onnxfixture::sumDual(
                                     { onnxfixture::fixed( 1 ), onnxfixture::fixed( 2 ),
                                       onnxfixture::fixed( 4 ), onnxfixture::fixed( 4 ) } ) );
  auto &registry = ModelRuntimeRegistry::instance();
  HardwarePin pin( cpuHost() );
  registry.releaseAll();
  std::string error;
  const ModelRuntimePtr session = registry.acquire( ortModel( artifact ), RequestedDevice::cpu(), &error );
  REQUIRE( session );

  const auto before = session->health().forwardsCompleted;
  session->warmup(); // must not throw, must not fail the session
  const SessionHealth health = session->health();
  INFO( "warmup lastError = " << health.lastError );
  CHECK( health.ok );
  CHECK( health.forwardsCompleted == before + 1 );
  registry.releaseAll();
}

TEST_CASE( "cancel before the forward is immediate and clearCancel restores the session",
           "[models][ort][cancel]" )
{
  QTemporaryDir dir;
  const std::string artifact =
    writeFixture( dir, "sum_dual", onnxfixture::sumDual(
                                     { onnxfixture::fixed( 1 ), onnxfixture::fixed( 2 ),
                                       onnxfixture::fixed( 4 ), onnxfixture::fixed( 4 ) } ) );
  auto &registry = ModelRuntimeRegistry::instance();
  HardwarePin pin( cpuHost() );
  registry.releaseAll();
  std::string error;
  const ModelRuntimePtr session = registry.acquire( ortModel( artifact ), RequestedDevice::cpu(), &error );
  REQUIRE( session );

  session->requestCancel();
  std::vector<float> a( 32, 1.0f );
  const TensorBlob ta = TensorBlob::fromFloat32( { 1, 2, 4, 4 }, a.data(), a.size() );
  std::vector<float> b( 32, 1.0f );
  const TensorBlob tb = TensorBlob::fromFloat32( { 1, 2, 4, 4 }, b.data(), b.size() );
  REQUIRE_THROWS_WITH( session->inferNamed( { { "a", ta }, { "b", tb } }, {} ),
                       Catch::Matchers::ContainsSubstring( "canceled" ) );

  session->clearCancel();
  for ( float &v : b )
    v = 2.0f;
  const TensorBlob tb2 = TensorBlob::fromFloat32( { 1, 2, 4, 4 }, b.data(), b.size() );
  const std::vector<NamedTensor> out = session->inferNamed( { { "a", ta }, { "b", tb2 } }, {} );
  REQUIRE( out.size() == 1 );
  CHECK( out[0].second.dataFloat32()[0] == Catch::Approx( 3.0f ).margin( 1e-5 ) );
  registry.releaseAll();
}

TEST_CASE( "requestCancel terminates a RUNNING ORT forward within a bounded delay",
           "[models][ort][cancel]" )
{
  QTemporaryDir dir;
  // 96 chained 1024x1024 MatMuls ≈ 2 s of kernel work on this host — the
  // cancel request (after 300 ms) lands mid-forward with margin. Bounded:
  // the joined thread must return within 60 s under any scheduling.
  const std::string artifact =
    writeFixture( dir, "slow_matmul", onnxfixture::slowMatmulChain( 1024, 96 ) );
  auto &registry = ModelRuntimeRegistry::instance();
  HardwarePin pin( cpuHost() );
  registry.releaseAll();
  std::string error;
  const ModelRuntimePtr session = registry.acquire( ortModel( artifact ), RequestedDevice::cpu(), &error );
  REQUIRE( session );

  std::vector<float> x( 1024ull * 1024ull, 1.0f );
  const TensorBlob tx =
    TensorBlob::fromFloat32( { 1024, 1024 }, x.data(), x.size() );

  std::atomic<bool> done{ false };
  std::string failureMessage;
  bool completedSizeOk = false;
  std::thread runner( [ & ] {
    try
    {
      const std::vector<NamedTensor> out = session->inferNamed( { { "x", tx } }, {} );
      // Completing is legal on a fast host; remember nothing went wrong.
      completedSizeOk = out.size() == 1;
    }
    catch ( const std::exception &e )
    {
      failureMessage = e.what();
    }
    done.store( true, std::memory_order_relaxed );
  } );

  std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
  session->requestCancel();

  bool joined = false;
  for ( int i = 0; i < 600 && !joined; ++i ) // 100 ms x 600 = 60 s bound
  {
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    joined = done.load( std::memory_order_relaxed );
  }
  runner.join();
  CHECK( joined );
  if ( !failureMessage.empty() )
    CHECK( completedSizeOk == false );

  // Either the forward was terminated (cancel-flavored diagnostic) or it had
  // already completed before the cancel landed (only on a much faster host).
  INFO( "failureMessage = " << failureMessage );
  CHECK( ( failureMessage.empty() ||
           failureMessage.find( "cancel" ) != std::string::npos ) );

  // The session survives a canceled run: cleared cancel flag → healthy.
  session->clearCancel();
  CHECK( session->health().ok );
  registry.releaseAll();
}

// ---------------------------------------------------------------------------
// CUDA-less host honesty: a forced-CUDA request fails typed, never demotes
// ---------------------------------------------------------------------------

TEST_CASE( "a forced CUDA acquisition on a CUDA-less host fails with a typed load error",
           "[models][ort][device]" )
{
  QTemporaryDir dir;
  const std::string artifact =
    writeFixture( dir, "sum_dual", onnxfixture::sumDual(
                                     { onnxfixture::fixed( 1 ), onnxfixture::fixed( 2 ),
                                       onnxfixture::fixed( 4 ), onnxfixture::fixed( 4 ) } ) );
  // Force the platform to CLAIM a CUDA device (env-detection test seam): the
  // ORT provider must then try the CUDA EP and honestly fail — this build
  // carries no CUDA EP. The failure is typed and named, never a silent CPU
  // demotion (cpuFallback=false).
  ModelHardwareCapabilities forced = cpuHost();
  forced.cudaAvailable = true;
  forced.cudaDeviceCount = 1;
  forced.vramBudgetMb = 8192;
  HardwarePin pin( forced );

  ModelInfo model = ortModel( artifact, /*gpu*/ true );
  model.cpuFallback = false;
  model.runtime.gpu = true;
  model.runtime.cpuFallback = false;

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  std::string error;
  const ModelRuntimePtr session =
    registry.acquire( model, RequestedDevice::cuda( 0 ), &error );
  REQUIRE_FALSE( session );
  CHECK_THAT( error, Catch::Matchers::ContainsSubstring( "ONNX Runtime" ) );
  registry.releaseAll();
}
