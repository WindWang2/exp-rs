// tests/test_r14_B_runtime_providers.cpp — R14-B provider-surface
// regressions: OpenCV inferMulti buffer detachment (shared-session race),
// the HTTP provider's no-dispatcher-thread typed refusal, and the VRAM
// ledger's randomized conservation invariant (extension of the #1455 fix).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/runtime/http_provider.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/opencv_dnn_runtime.h"
#include "operators/runtime/tensor_blob.h"

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::OpenCvDnnRuntime;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

struct HardwarePin
{
  HardwarePin( const ModelHardwareCapabilities &caps )
  {
    ModelRuntimeRegistry::instance().setHardwareForTest( caps );
  }
  ~HardwarePin() { ModelRuntimeRegistry::instance().setHardwareForTest( std::nullopt ); }
};

struct RegistryReset
{
  ~RegistryReset()
  {
    ModelRuntimeRegistry::instance().releaseAll();
    ModelRuntimeRegistry::instance().vramLedger().reset();
  }
};

} // namespace

// ---------------------------------------------------------------------------
// OpenCV DNN inferMulti detachment
// ---------------------------------------------------------------------------

TEST_CASE( "#r14B OpenCV inferMulti returns detached outputs safe across forwards",
           "[models][providers][r14B]" )
{
  REQUIRE( QFile::exists( identityModelPath() ) );

  OpenCvDnnRuntime runtime( identityModelPath().toStdString(), /*modelWantsGpu=*/false,
                            ModelHardwareCapabilities{} );
  std::string loadError;
  REQUIRE( runtime.load( &loadError ) );
  INFO( loadError );

  const auto blobOf = []( float value ) {
    const int dims[4] = { 1, 1, 4, 4 };
    cv::Mat blob( 4, dims, CV_32F );
    blob.setTo( cv::Scalar( value ) );
    return blob;
  };

  auto first = runtime.inferMulti( { { std::string(), blobOf( 0.25f ) } } );
  REQUIRE( first.size() == 1 );
  REQUIRE( first[0].total() > 0 );
  const cv::Mat snapshot = first[0].clone();
  CHECK( snapshot.ptr<float>()[0] == Catch::Approx( 0.25f ).margin( 1e-5 ) );

  // A second forward on the SHARED session must not tear the first result:
  // the old implementation returned headers onto the net's internal buffers.
  auto second = runtime.inferMulti( { { std::string(), blobOf( 0.75f ) } } );
  REQUIRE( second.size() == 1 );
  CHECK( second[0].ptr<float>()[0] == Catch::Approx( 0.75f ).margin( 1e-5 ) );

  double maxDrift = 0.0;
  for ( std::size_t i = 0; i < first[0].total(); ++i )
    maxDrift = std::max( maxDrift,
                         static_cast<double>(
                             std::abs( first[0].ptr<float>()[i] - snapshot.ptr<float>()[i] ) ) );
  CHECK( maxDrift < 1e-6 );
}

TEST_CASE( "#r14B OpenCV infer refuses non-float32 blobs with a typed error",
           "[models][providers][r14B]" )
{
  OpenCvDnnRuntime runtime( identityModelPath().toStdString(), false,
                            ModelHardwareCapabilities{} );
  std::string loadError;
  REQUIRE( runtime.load( &loadError ) );

  // 4-D uint8: passes the dims gate, must fail the float32 gate (a 2-D blob
  // would hide behind the pre-existing dims!=4 refusal).
  const int dims[4] = { 1, 1, 4, 4 };
  cv::Mat bytes( 4, dims, CV_8U );
  bytes.setTo( cv::Scalar( 7 ) );
  REQUIRE_THROWS_AS( runtime.infer( bytes ), std::runtime_error );
}

// ---------------------------------------------------------------------------
// HTTP provider on a dispatcher-less thread
// ---------------------------------------------------------------------------

TEST_CASE( "#r14B HTTP inference refuses typed on threads without a Qt event dispatcher",
           "[models][providers][http][r14B]" )
{
  ModelRuntimeRegistry::instance().releaseAll();
  sicnu::operators::runtime::registerHttpProvider( ModelRuntimeRegistry::instance() );

  ModelInfo info;
  info.name = "remote-http";
  info.framework = "http";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = "/nonexistent/weights.onnx";
  info.runtime.provider.url = "http://127.0.0.1:9/never-reached";

  std::string error;
  const auto session = ModelRuntimeRegistry::instance().acquire( info, &error );
  // The registry's default provider chain may or may not carry "http"; when
  // the acquisition refused, this environment cannot exercise the session
  // surface and the case degrades to asserting the typed refusal.
  if ( !session )
  {
    INFO( "acquire refused: " << error );
    CHECK( !error.empty() );
    return;
  }
  REQUIRE( session->framework() == "http" );

  // A plain std::thread has NO Qt event dispatcher: the QTimer/socket
  // notifiers driving the exchange can never fire and the request used to
  // hang FOREVER (the timeout contract was void). It must now return a
  // typed error promptly instead. State is heap-shared so the detached
  // fallback path (RED on master) never references case locals.
  struct Shared
  {
    std::atomic<bool> returned{ false };
    std::atomic<bool> typedRefusal{ false };
    std::string message;
    std::mutex messageMutex;
  };
  const auto shared = std::make_shared<Shared>();
  std::thread worker( [ session, shared ] {
    try
    {
      const int dims[4] = { 1, 1, 2, 2 };
      cv::Mat blob( 4, dims, CV_32F );
      blob.setTo( cv::Scalar( 0.f ) );
      session->inferNamed(
        { { std::string(),
            sicnu::operators::runtime::TensorBlob::fromMat( blob ) } }, {} );
    }
    catch ( const std::exception &e )
    {
      std::lock_guard<std::mutex> lock( shared->messageMutex );
      shared->message = e.what();
      if ( std::string( e.what() ).find( "event-dispatcher" ) != std::string::npos )
        shared->typedRefusal.store( true );
    }
    shared->returned.store( true );
  } );

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 8 );
  while ( !shared->returned.load() && std::chrono::steady_clock::now() < deadline )
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );

  if ( !shared->returned.load() )
  {
    // Cannot join a hung thread without blocking forever — leave it detached;
    // the CHECK below fails the case (this is the RED shape on master).
    worker.detach();
    FAIL( "HTTP inferNamed did not return within 8s on a dispatcher-less thread" );
    return;
  }
  worker.join();
  REQUIRE( shared->returned.load() );
  INFO( "error: " << shared->message );
  CHECK( shared->typedRefusal.load() );
}

// ---------------------------------------------------------------------------
// VRAM ledger randomized conservation (WP-05 extension over the #1455 fix)
// ---------------------------------------------------------------------------

namespace {

/// A GPU-flavored runtime that dies on demand (corpse-recycle shape).
class StressRuntime final : public sicnu::operators::runtime::IModelRuntime
{
  public:
    StressRuntime( std::shared_ptr<std::atomic<bool>> dead ) : m_dead( std::move( dead ) ) {}
    std::string framework() const override { return "stressfw"; }
    std::string backendName() const override { return "stress-fake"; }
    std::string deviceName() const override { return "cuda"; }
    std::string artifactPath() const override { return "stress-fixture"; }
    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }
    bool permanentlyUnavailable() const override { return m_dead->load(); }
    std::shared_ptr<std::atomic<bool>> m_dead;
};

} // namespace

TEST_CASE( "#r14B VRAM ledger conserves under randomized acquire/release pressure",
           "[models][runtime][ledger][r14B]" )
{
  RegistryReset reset;
  auto &registry = ModelRuntimeRegistry::instance();

  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 512;
  const HardwarePin pin( hw );
  registry.vramLedger().reset();
  registry.vramLedger().setCapacity( 0, 256 );
  registry.setMaxCachedSessions( 4 );

  auto dead = std::make_shared<std::atomic<bool>>( false );
  registry.registerProvider(
    "stressfw",
    [ dead ]( const ModelInfo &, const ModelHardwareCapabilities &, std::string * )
      -> ModelRuntimePtr { return std::make_shared<StressRuntime>( dead ); } );

  std::vector<ModelInfo> models;
  for ( int i = 0; i < 3; ++i )
  {
    ModelInfo info;
    info.name = "stress-" + std::to_string( i );
    info.framework = "stressfw";
    info.readiness = ModelReadiness::Ready;
    info.runtime.gpu = true;
    info.runtime.estimatedVramMb = 32;
    info.resolvedArtifactPath = "/tmp/stress-" + std::to_string( i ) + ".onnx";
    info.contentDigest = "stress-digest-" + std::to_string( i );
    models.push_back( info );
  }

  // Deterministic schedule of concurrent acquisitions; holders park sessions
  // beyond cache eviction (the ensemble-member shape), then drop them.
  std::vector<ModelRuntimePtr> parked;
  std::mutex parkedMutex;
  std::atomic<bool> stop{ false };
  std::vector<std::thread> workers;
  for ( int t = 0; t < 4; ++t )
  {
    workers.emplace_back( [ &, t ] {
      std::mt19937 rng( 0xB14D + static_cast<unsigned>( t ) );
      std::uniform_int_distribution<int> pick( 0, 2 );
      std::uniform_int_distribution<int> dice( 0, 9 );
      while ( !stop.load() )
      {
        ModelInfo model = models[static_cast<std::size_t>( pick( rng ) )];
        std::string error;
        auto session = registry.acquire( model, sicnu::operators::runtime::RequestedDevice::cuda( 0 ),
                                         &error );
        if ( session && dice( rng ) < 3 )
        {
          std::lock_guard<std::mutex> lock( parkedMutex );
          parked.push_back( session ); // hold past LRU eviction
        }
        if ( dice( rng ) == 0 )
          dead->store( true ); // kill every live session (corpse-recycle path)
        else if ( dice( rng ) == 1 )
          dead->store( false );
        if ( dice( rng ) < 2 )
        {
          std::lock_guard<std::mutex> lock( parkedMutex );
          if ( !parked.empty() )
          {
            std::uniform_int_distribution<std::size_t> dropIdx(
              0, parked.size() - 1 );
            parked.erase( parked.begin() + static_cast<std::ptrdiff_t>( dropIdx( rng ) ) );
          }
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
      }
    } );
  }

  std::this_thread::sleep_for( std::chrono::seconds( 3 ) );
  stop.store( true );
  for ( auto &w : workers )
    w.join();
  {
    std::lock_guard<std::mutex> lock( parkedMutex );
    parked.clear(); // every externally-held session drops
  }
  registry.releaseAll();
  // Conservation: every ticket issued must have been unwound exactly once —
  // no leaked reservations (under-count → GPU over-admission) and no
  // negative residue (over-release erasing a replacement's booking).
  CHECK( registry.vramLedger().reservedMb( 0 ) == 0 );
  for ( const auto &state : registry.deviceReport() )
    if ( state.index == 0 )
      CHECK( state.holders == 0 );
}
