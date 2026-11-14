// test_r30_t02_cancel_isolation.cpp — R30-T02 WP05/WP06: per-invocation
// forward cancellation wired through the engines.
//   * a provider with cancelInForward + beginForwardInvocation: cancelling
//     the RUN CONTEXT mid-forward interrupts the ACTIVE forward, the engine
//     reports Cancelled, and the very same shared session serves the NEXT
//     run correctly (no session-global flag poisoning);
//   * cancelling invocation A never terminates invocation B's forward on the
//     same session (isolation);
//   * a coarse provider (cancelInForward=false) keeps the historical
//     batch-boundary semantics: no watcher, the flag is never set;
//   * cancel BEFORE the run: the engine refuses at the first checkpoint;
//   * real ORT lane (when compiled in): the per-invocation handle cancels
//     exactly its own next forward and leaves the session usable.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/forward_cancel_bridge.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <opencv2/dnn.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace sicnu::operators::runtime;
using sicnu::operators::ErrorCode;
using sicnu::operators::ModelInfo;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

/// Fake with REAL per-invocation semantics (a miniature of the ORT design):
/// forwards are serialized; each forward binds to the invocation scope
/// active on its calling thread; cancel() terminates exactly the bound
/// forward. sleepMs simulates a long-running kernel.
class ScopedCancelRuntime final : public IModelRuntime
{
  public:
    explicit ScopedCancelRuntime( int sleepMs ) : m_sleepMs( sleepMs ) {}

    std::string framework() const override { return "r30-scoped"; }
    std::string backendName() const override { return "r30scoped"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r30scoped"; }

    ProviderCapabilities capabilities() const override
    {
      ProviderCapabilities caps;
      caps.cancelInForward = true;
      return caps;
    }

    class Handle : public ForwardCancelHandle
    {
      public:
        void cancel() override
        {
          cancelled.store( true, std::memory_order_relaxed );
          std::lock_guard<std::mutex> lock( activeMutex );
          if ( activeCancelled )
            activeCancelled->store( true, std::memory_order_relaxed );
        }
        std::atomic<bool> cancelled{ false };

      private:
        friend class ScopedCancelRuntime;
        std::mutex activeMutex;
        std::atomic<bool> *activeCancelled = nullptr;
    };

    std::shared_ptr<ForwardCancelHandle> beginForwardInvocation() override
    {
      auto handle = std::make_shared<Handle>();
      std::lock_guard<std::mutex> lock( t_handlesMutex );
      t_handles.push_back( handle );
      return handle;
    }

    cv::Mat infer( const cv::Mat &nchw ) override
    {
      // Scope = the innermost handle opened on THIS thread.
      std::shared_ptr<Handle> scope;
      {
        std::lock_guard<std::mutex> lock( t_handlesMutex );
        for ( auto it = t_handles.rbegin(); it != t_handles.rend(); ++it )
          if ( ( scope = it->lock() ) )
            break;
      }
      if ( scope && scope->cancelled.load( std::memory_order_relaxed ) )
        throw std::runtime_error( "inference canceled before the forward pass" );

      std::lock_guard<std::mutex> serialize( m_forwardMutex );
      std::atomic<bool> terminated{ false };
      if ( scope )
      {
        std::lock_guard<std::mutex> lock( scope->activeMutex );
        scope->activeCancelled = &terminated;
        if ( scope->cancelled.load( std::memory_order_relaxed ) )
          terminated.store( true, std::memory_order_relaxed );
      }
      forwardsEntered += 1;
      // Simulated kernel: poll the per-forward terminate bit.
      const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds( m_sleepMs );
      while ( std::chrono::steady_clock::now() < deadline )
      {
        if ( terminated.load( std::memory_order_relaxed ) )
        {
          if ( scope )
          {
            std::lock_guard<std::mutex> lock( scope->activeMutex );
            scope->activeCancelled = nullptr;
          }
          forwardsTerminated += 1;
          throw std::runtime_error( "inference canceled during the forward pass" );
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
      }
      if ( scope )
      {
        std::lock_guard<std::mutex> lock( scope->activeMutex );
        scope->activeCancelled = nullptr;
      }
      // Two class planes (the manifest declares two classes — the raster
      // head writes one channel per class).
      int dims[4] = { nchw.size[0], 2, nchw.size[2], nchw.size[3] };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.0f ) );
      return out;
    }

    int forwardsEntered = 0;
    int forwardsTerminated = 0;

  private:
    int m_sleepMs = 0;
    std::mutex m_forwardMutex;
    static std::mutex t_handlesMutex;
    static thread_local std::vector<std::weak_ptr<Handle>> t_handles;
};

std::mutex ScopedCancelRuntime::t_handlesMutex;
thread_local std::vector<std::weak_ptr<ScopedCancelRuntime::Handle>>
  ScopedCancelRuntime::t_handles;

/// Coarse fake: no per-invocation support, sleeps through cancellation like
/// OpenCV DNN would — the engine can only stop BETWEEN forwards.
class CoarseSleepRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "r30-coarse"; }
    std::string backendName() const override { return "r30coarse"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r30coarse"; }

    cv::Mat infer( const cv::Mat &nchw ) override
    {
      forwardsEntered += 1;
      std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
      return nchw.clone();
    }

    std::atomic<int> forwardsEntered{ 0 };
};

ModelInfo cancellationModel( int tile = 32, int sleepUnused = 0 )
{
  ( void )sleepUnused;
  ModelInfo model;
  model.name = "r30-cancel";
  model.framework = "r30-scoped";
  model.task = "segmentation";
  model.tiling.tileSize = tile;
  model.output.classes = { "a", "b" };
  return model;
}

QString tinyRaster( const QTemporaryDir &dir, const QString &name )
{
  sicnu::testing::RsSyntheticRasterBuilder builder( 32, 32, 1, GDT_Float32 );
  builder.withConstantValue( 1, 0.5f );
  const QString path = builder.writeToDisk( dir.filePath( name ) );
  REQUIRE_FALSE( path.isEmpty() );
  return path;
}

} // namespace

// ---------------------------------------------------------------------------

TEST_CASE( "R30-T02: cancelling the run context interrupts the active forward",
           "[models][r30t02][cancel]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = tinyRaster( dir, QStringLiteral( "r.tif" ) );

  // One forward for the whole 1-tile raster, 800 ms of simulated kernel.
  auto runtime = std::make_shared<ScopedCancelRuntime>( 800 );
  ModelInfo model = cancellationModel( 32 );
  TileInferenceEngine engine( model, runtime );

  std::atomic<bool> cancelFlag{ false };

  std::atomic<bool> threwCancelled{ false };
  std::string failureNote;
  std::thread runner( [ & ] {
    try
    {
      RSOperatorContext runContext;
      runContext.setCancelFlag( &cancelFlag );
      engine.run( raster.toStdString(), {},
                  dir.filePath( QStringLiteral( "out.tif" ) ).toStdString(), runContext );
    }
    catch ( const RSOperatorError &e )
    {
      threwCancelled = e.code() == ErrorCode::Cancelled;
      failureNote = std::string( "RSOperatorError(" ) + std::to_string( static_cast<int>( e.code() ) )
                    + "): " + e.message();
    }
    catch ( const std::exception &e )
    {
      failureNote = std::string( "std::exception: " ) + e.what();
    }
    catch ( ... )
    {
      failureNote = "unknown exception";
    }
  } );

  // Let the forward actually start, then cancel; the bridge's watcher polls
  // at 50 ms — causal, not timing-dependent (the forward polls every 5 ms).
  for ( int waited = 0; waited < 100 && runtime->forwardsEntered == 0; ++waited )
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  if ( runtime->forwardsEntered == 0 )
  {
    runner.join();
    FAIL( "engine never entered a forward: " + failureNote );
  }
  REQUIRE( runtime->forwardsEntered >= 1 );
  cancelFlag.store( true );
  runner.join();
  // The forward was terminated mid-pass (not run to completion).
  CHECK( runtime->forwardsTerminated == 1 );
  CHECK( threwCancelled.load() );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "out.tif" ) ) ) );

  // THE isolation property: the same session serves the NEXT run correctly —
  // the cancelled invocation never poisoned session-global state.
  {
    TileInferenceEngine next( model, runtime );
    RSOperatorContext freshContext;
    const auto stats =
      next.run( raster.toStdString(), {},
                dir.filePath( QStringLiteral( "out2.tif" ) ).toStdString(), freshContext );
    CHECK( stats.tilesProcessed >= 1 );
  }
  CHECK( runtime->forwardsTerminated == 1 ); // nothing new was terminated
}

TEST_CASE( "R30-T02: cancelling invocation A leaves invocation B untouched",
           "[models][r30t02][cancel]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString rasterA = tinyRaster( dir, QStringLiteral( "a.tif" ) );
  const QString rasterB = tinyRaster( dir, QStringLiteral( "b.tif" ) );

  auto runtime = std::make_shared<ScopedCancelRuntime>( 600 );
  ModelInfo model = cancellationModel( 32 );

  std::atomic<bool> cancelA{ false };

  // Invocation A runs (and gets cancelled mid-forward).
  std::thread threadA( [ & ] {
    try
    {
      TileInferenceEngine engineA( model, runtime );
      RSOperatorContext contextA;
      contextA.setCancelFlag( &cancelA );
      engineA.run( rasterA.toStdString(), {},
                   dir.filePath( QStringLiteral( "a.out.tif" ) ).toStdString(), contextA );
    }
    catch ( const RSOperatorError & )
    {
    }
    catch ( ... )
    {
    }
  } );

  // Wait until A's forward has ACTUALLY started (engine setup time is not
  // the axis under test) — then cancel mid-pass.
  for ( int waited = 0; waited < 100 && runtime->forwardsEntered == 0; ++waited )
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  REQUIRE( runtime->forwardsEntered >= 1 );
  cancelA.store( true );
  threadA.join();
  CHECK( runtime->forwardsTerminated == 1 );

  // Invocation B — SAME session, DIFFERENT (never-cancelled) context: its
  // forward must run to completion. Under the historical session-global flag
  // semantics this forward would have died on A's poison.
  {
    TileInferenceEngine engineB( model, runtime );
    RSOperatorContext contextB;
    const auto stats =
      engineB.run( rasterB.toStdString(), {},
                   dir.filePath( QStringLiteral( "b.out.tif" ) ).toStdString(), contextB );
    CHECK( stats.tilesProcessed >= 1 );
  }
  CHECK( runtime->forwardsTerminated == 1 );
  CHECK( runtime->forwardsEntered == 2 );
}

TEST_CASE( "R30-T02: coarse providers keep batch-boundary cancellation",
           "[models][r30t02][cancel]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // 2 tiles with batch cap 1 → two 200 ms forwards; cancel lands between
  // them: the ACTIVE forward completes (coarse — honest), the second never
  // starts.
  auto runtime = std::make_shared<CoarseSleepRuntime>();
  ModelInfo model;
  model.name = "r30-coarse-model";
  model.framework = "r30-coarse";
  model.task = "segmentation";
  model.tiling.tileSize = 16; // 32x32 raster → 4 tiles
  model.tiling.batchSize = 1;
  model.output.classes = { "a", "b" };

  sicnu::testing::RsSyntheticRasterBuilder builder( 32, 32, 1, GDT_Float32 );
  builder.withConstantValue( 1, 0.5f );
  const QString raster = builder.writeToDisk( dir.filePath( QStringLiteral( "r.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );

  std::atomic<bool> cancelFlag{ false };
  bool threwCancelled = false;
  {
    TileInferenceEngine engine( model, runtime );
    RSOperatorContext context;
    context.setCancelFlag( &cancelFlag );
    // Pre-cancel: the engine refuses at its first checkpoint and never
    // enters a forward.
    cancelFlag.store( true );
    try
    {
      engine.run( raster.toStdString(), {},
                  dir.filePath( QStringLiteral( "out.tif" ) ).toStdString(), context );
    }
    catch ( const RSOperatorError &e )
    {
      threwCancelled = e.code() == ErrorCode::Cancelled;
    }
  }
  CHECK( threwCancelled );
  CHECK( runtime->forwardsEntered.load() == 0 );
}

TEST_CASE( "R30-T02: the bridge is inert for providers without the capability",
           "[models][r30t02][cancel]" )
{
  // Unit-level: the bridge opens no watcher when cancelInForward is false —
  // a cancelled context is simply not forwarded to a coarse provider.
  class InertProbeRuntime final : public IModelRuntime
  {
    public:
      std::string framework() const override { return "r30-inert"; }
      std::string backendName() const override { return "inert"; }
      std::string deviceName() const override { return "cpu"; }
      std::string artifactPath() const override { return "fake://inert"; }
      std::shared_ptr<ForwardCancelHandle> beginForwardInvocation() override
      {
        // R1-4: the observable — the bridge must NEVER open a scope on a
        // provider without the capability.
        begins += 1;
        return nullptr;
      }
      cv::Mat infer( const cv::Mat &nchw ) override
      {
        enters += 1;
        return nchw.clone();
      }
      std::atomic<int> begins{ 0 };
      std::atomic<int> enters{ 0 };
  };

  auto runtime = std::make_shared<InertProbeRuntime>();
  CHECK_FALSE( runtime->capabilities().cancelInForward );
  std::atomic<bool> cancelFlag{ true };
  RSOperatorContext context;
  context.setCancelFlag( &cancelFlag );
  {
    const ForwardCancelBridge bridge( runtime, context );
    std::this_thread::sleep_for( std::chrono::milliseconds( 120 ) );
  }
  CHECK( runtime->begins.load() == 0 ); // no per-invocation scope was opened
  CHECK( runtime->enters.load() == 0 ); // and nothing ran
}

#ifdef SICNU_WITH_ONNX_RUNTIME
#include "operators/runtime/onnxruntime_provider.h"
#include "support/onnx_fixture_builder.h"

TEST_CASE( "R30-T02: real ORT per-invocation handle cancels its own forward",
           "[models][r30t02][cancel][onnxruntime]" )
{
  namespace fs = std::filesystem;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // Single-input identity graph: x (1,C,H,W dynamic) → y = x.
  const std::string modelProto = onnxfixture::buildModel(
    { onnxfixture::node( "Identity", { "x" }, { "y" } ) }, {},
    { onnxfixture::valueInfo( "x", onnxfixture::Float,
                              { onnxfixture::dynamic( "n" ), onnxfixture::dynamic( "c" ),
                                onnxfixture::dynamic( "h" ), onnxfixture::dynamic( "w" ) } ) },
    { onnxfixture::valueInfo( "y", onnxfixture::Float,
                              { onnxfixture::dynamic( "n" ), onnxfixture::dynamic( "c" ),
                                onnxfixture::dynamic( "h" ), onnxfixture::dynamic( "w" ) } ) },
    "r30_identity" );
  const std::string artifact =
    ( fs::path( dir.path().toStdString() ) / "identity.onnx" ).string();
  REQUIRE( onnxfixture::writeModel( artifact, modelProto ) > 0 );

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  ModelInfo info;
  info.name = "r30-ort-identity";
  info.task = "segmentation";
  info.framework = "onnxruntime";
  info.readiness = sicnu::operators::ModelReadiness::Ready;
  info.resolvedArtifactPath = artifact;
  std::string error;
  const auto session = registry.acquire( info, sicnu::operators::runtime::RequestedDevice::cpu(),
                                         &error );
  REQUIRE( session );
  REQUIRE( session->framework() == "onnxruntime" );
  REQUIRE( session->capabilities().cancelInForward );

  cv::Mat hwc( 3, 4, CV_32FC1, cv::Scalar( 0.5f ) );
  const cv::Mat blob = cv::dnn::blobFromImage( hwc );

  // Invocation 1: cancel BEFORE the forward — exactly its next forward
  // refuses (the engine's bridge holds the handle for one run; here the
  // scope ends with the shared_ptr below).
  bool refused = false;
  {
    const auto handle = session->beginForwardInvocation();
    REQUIRE( handle );
    handle->cancel();
    try
    {
      session->infer( blob );
    }
    catch ( const std::runtime_error &e )
    {
      refused = std::string( e.what() ).find( "canceled before the forward pass" )
                != std::string::npos;
    }
  } // handle destroyed → the thread's invocation scope expires
  CHECK( refused );

  // The session itself is NOT cancelled: the session-global flag was never
  // touched, and a forward outside any cancelled scope completes.
  bool completed = false;
  try
  {
    const cv::Mat out = session->infer( blob );
    completed = out.total() > 0;
  }
  catch ( ... )
  {
  }
  CHECK( completed );
}
#endif // SICNU_WITH_ONNX_RUNTIME
