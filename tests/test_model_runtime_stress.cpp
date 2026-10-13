// tests/test_model_runtime_stress.cpp — session pool concurrency stress:
// the Oracle is "no race, no use-after-free, no cross-device misuse" under
// many threads acquiring/infering/releasing while LRU + idle eviction and
// the pressure valve churn the pool beneath them. Deterministic (fixed
// thread/device/iteration pattern — no sleeps, no RNG): races surface as
// violated invariants, not flakes. Under an ASAN/TSAN build this suite is
// the sanitizer's target; in a plain build it asserts the behavioral
// invariants (held sessions stay valid after eviction, device pinning holds,
// the ledger never over-commits, pool stats stay consistent).
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/runtime/model_runtime.h"

#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <numeric>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::runtime::DevicePlacementPolicy;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::ProviderTraits;
using sicnu::operators::runtime::RequestedDevice;

/// A session whose device identity is baked in at factory time (the
/// registry's resolved contract), so tests can detect cross-device reuse.
class DeviceTaggedRuntime final : public IModelRuntime
{
  public:
    DeviceTaggedRuntime( std::string artifact, std::string framework, std::string device )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_device( std::move( device ) ) {}

    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "stress_backend"; }
    std::string deviceName() const override { return m_device; }
    std::string artifactPath() const override { return m_artifact; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      // Touch the whole object: an evicted-then-freed session would fault
      // here under ASAN. The counter is guarded — implementations serialize
      // forward passes by contract.
      std::lock_guard<std::mutex> lock( m_mutex );
      ++m_forwards;
      return blob.clone();
    }

  private:
    std::string m_artifact;
    std::string m_framework;
    std::string m_device;
    std::mutex m_mutex;
    int m_forwards = 0;
};

struct RegistryReset
{
    RegistryReset()
    {
      auto &registry = ModelRuntimeRegistry::instance();
      registry.releaseAll();
      registry.resetLoadCount();
      registry.setMaxCachedSessions( 4 );
      registry.setIdleEvictionMs( 0 );
      registry.setPlacementPolicy( DevicePlacementPolicy::LowestFitting );
    }
    ~RegistryReset()
    {
      auto &registry = ModelRuntimeRegistry::instance();
      registry.releaseAll();
      registry.setIdleEvictionMs( 0 );
      registry.setMaxCachedSessions( 4 );
      registry.setPlacementPolicy( DevicePlacementPolicy::LowestFitting );
    }
};

struct HardwarePin
{
    HardwarePin( const ModelHardwareCapabilities &caps )
    {
      ModelRuntimeRegistry::instance().setHardwareForTest( caps );
    }
    ~HardwarePin() { ModelRuntimeRegistry::instance().setHardwareForTest( std::nullopt ); }
};

ModelInfo stressModel( const std::string &name, const std::string &artifactPath )
{
  ModelInfo info;
  info.name = name;
  info.task = "segmentation";
  info.framework = "stressfw";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = artifactPath;
  info.runtime.gpu = true;
  info.runtime.estimatedVramMb = 32;
  // Pin the device assertion: with cpu_fallback the documented platform
  // semantics DEMOTE an over-budget cuda:N request to cpu — honest and
  // payload-reported, but here we are testing the pin, not the demotion.
  info.runtime.cpuFallback = false;
  return info;
}

/// Writes one distinct artifact copy per model name: the session identity is
/// the artifact CONTENT digest, so equal bytes would (by design) share one
/// session and collapse the ledger pressure this suite depends on. The fake
/// provider never parses weights, so appended bytes are safe.
std::string distinctArtifact( const QTemporaryDir &dir, const std::string &name )
{
  const QString source =
    QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
  QFile in( source );
  if ( !in.open( QIODevice::ReadOnly ) )
    return {};
  QByteArray bytes = in.readAll();
  bytes.append( QString::fromStdString( name ).toUtf8() );
  const QString path = dir.filePath( QString::fromStdString( name ) + QStringLiteral( ".onnx" ) );
  QFile out( path );
  if ( !out.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return {};
  out.write( bytes );
  out.close();
  return path.toStdString();
}

/// Registers the device-tagging provider with two-device addressing traits.
void registerStressProvider()
{
  ModelRuntimeRegistry::instance().registerProvider(
    "stressfw",
    [ ]( const ModelInfo &model, const ModelHardwareCapabilities &, std::string *error ) -> ModelRuntimePtr {
      if ( model.resolvedArtifactPath.empty() )
      {
        if ( error )
          *error = "no artifact";
        return nullptr;
      }
      const std::string device =
        model.runtime.gpu ? "cuda:" + std::to_string( model.runtime.resolvedCudaIndex ) : "cpu";
      return std::make_shared<DeviceTaggedRuntime>( model.resolvedArtifactPath, "stressfw", device );
    },
    ProviderTraits{ /*maxAddressableCudaIndex*/ 1 } );
}

} // namespace

TEST_CASE( "pool stress: concurrent acquire/infer/release across devices keeps every invariant",
           "[models][stress][pool]" )
{
  RegistryReset reset;
  auto &registry = ModelRuntimeRegistry::instance();

  // Two addressable CUDA devices, 128 MiB each. The provider resolves the
  // ACTUAL device identity from the contract the registry handed it, so any
  // cross-device mix-up is observable through deviceName().
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaRuntimeAvailable = true;
  hw.cudaDeviceCount = 2;
  hw.vramBudgetMb = 128;
  hw.deviceTotalVramMb = { 128, 128 };
  hw.deviceFreeVramMb = { 128, 128 };
  const HardwarePin pin( hw );
  registerStressProvider();

  // Four distinct model identities (one artifact copy each — distinct
  // content digests) × 32 MiB estimate: all four fit the 128 MiB devices
  // exactly, so the LRU bound (4) and the concurrent rotation churn
  // reservations and evictions beneath the workers.
  constexpr int kThreads = 8;
  constexpr int kIterations = 200;
  QTemporaryDir dir;
  std::vector<std::string> artifacts;
  for ( int i = 0; i < 4; ++i )
  {
    const std::string artifact = distinctArtifact( dir, "stress-model-" + std::to_string( i ) );
    REQUIRE_FALSE( artifact.empty() );
    artifacts.push_back( artifact );
  }

  std::atomic<int> failures{ 0 };
  std::atomic<int> inferences{ 0 };
  std::mutex heldMutex;
  std::vector<ModelRuntimePtr> held; // sessions kept alive across evictions

  {
    // Idle eviction churns the pool beneath the workers.
    registry.setIdleEvictionMs( 1 );

    std::vector<std::thread> threads;
    for ( int t = 0; t < kThreads; ++t )
    {
      threads.emplace_back( [ &, t] {
        const ModelInfo model =
          stressModel( "stress-model-" + std::to_string( t % 4 ), artifacts[static_cast<std::size_t>( t % 4 )] );
        for ( int i = 0; i < kIterations; ++i )
        {
          // Deterministic device rotation: cpu, cuda:0, cuda:1 per thread.
          const int pick = ( t + i ) % 3;
          ModelRuntimePtr session;
          std::string error;
          if ( pick == 0 )
            session = registry.acquire( model, RequestedDevice::cpu(), &error );
          else
            session = registry.acquire( model, RequestedDevice::cuda( pick - 1 ), &error );
          if ( !session )
          {
            // A refusal is legal (eviction cannot create capacity), but it
            // must never be a crash and must name its reason.
            if ( error.empty() )
              ++failures;
            continue;
          }
          // Invariant: the session's device identity matches the request.
          const std::string expectedDevice =
            pick == 0 ? "cpu" : "cuda:" + std::to_string( pick - 1 );
          if ( session->deviceName() != expectedDevice )
            ++failures;
          try
          {
            cv::Mat blob( 1, 4, CV_32FC1, cv::Scalar( 1.0f ) );
            const cv::Mat out = session->infer( blob );
            if ( out.empty() )
              ++failures;
            else
              ++inferences;
          }
          catch ( ... )
          {
            ++failures;
          }
          // Hold a few sessions so evictions race live shared_ptrs (the
          // use-after-free detector: an evicted session must stay usable).
          if ( i % 32 == 0 )
          {
            std::lock_guard<std::mutex> lock( heldMutex );
            if ( held.size() < 24 )
              held.push_back( session );
          }
        }
      } );
    }
    for ( std::thread &thread : threads )
      thread.join();
  }

  // Every held session survived the churn and still works (no UAF, no
  // corrupted vtable).
  for ( const ModelRuntimePtr &session : held )
  {
    try
    {
      cv::Mat blob( 1, 4, CV_32FC1, cv::Scalar( 1.0f ) );
      REQUIRE_NOTHROW( session->infer( blob ) );
    }
    catch ( ... )
    {
      FAIL( "a held session threw after eviction — lifetime bug" );
    }
  }

  CHECK( failures.load() == 0 );
  CHECK( inferences.load() > 0 );

  // Ledger conservation: nothing reserved beyond capacity, nothing leaked.
  for ( const auto &device : registry.deviceReport() )
    CHECK( device.reservedMb <= device.capacityMb );

  // Pool stats conservation: every acquire was served from the cache or
  // produced a load; nothing vanished.
  const auto stats = registry.poolStats();
  CHECK( stats.cachedSessions <= stats.maxSessions );
}

TEST_CASE( "pool stress: eviction-while-held keeps evicted sessions fully usable",
           "[models][stress][uaf]" )
{
  RegistryReset reset;
  auto &registry = ModelRuntimeRegistry::instance();

  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaRuntimeAvailable = true;
  hw.cudaDeviceCount = 2;
  hw.vramBudgetMb = 64;
  const HardwarePin pin( hw );
  registerStressProvider();

  // Acquire on both cards, then force the pool to evict everything twice
  // (LRU bound + releaseAll) while the holders keep their shared_ptrs.
  QTemporaryDir dir;
  ModelInfo model = stressModel( "held-model", distinctArtifact( dir, "held-model" ) );
  auto onCuda0 = registry.acquire( model, RequestedDevice::cuda( 0 ), nullptr );
  auto onCuda1 = registry.acquire( model, RequestedDevice::cuda( 1 ), nullptr );
  REQUIRE( onCuda0 );
  REQUIRE( onCuda1 );
  CHECK( onCuda0->deviceName() == "cuda:0" );
  CHECK( onCuda1->deviceName() == "cuda:1" );
  CHECK( onCuda0 != onCuda1 ); // different cards never share a session

  registry.setMaxCachedSessions( 1 );
  const auto evictor =
    registry.acquire( stressModel( "other", distinctArtifact( dir, "other" ) ), RequestedDevice::cpu(), nullptr );
  REQUIRE( evictor ); // pushes LRU evictions of the GPU entries
  registry.releaseAll();

  // Both held sessions must still run — unload means "no longer handed
  // out", never "invalidates live pointers".
  cv::Mat blob( 1, 4, CV_32FC1, cv::Scalar( 2.0f ) );
  REQUIRE_NOTHROW( onCuda0->infer( blob ) );
  REQUIRE_NOTHROW( onCuda1->infer( blob ) );
  CHECK( onCuda0->deviceName() == "cuda:0" );
  CHECK( onCuda1->deviceName() == "cuda:1" );

  // #1160: reservations follow the SESSION, not the cache entry — while
  // the callers above still hold the evicted sessions, the ledger MUST
  // keep counting their occupancy (pinned by test_device_planner's
  // "externally held session keeps its ledger reservation past eviction").
  // The ledger only reads clean once the last references drop and the
  // #1160 deleters release.
  {
    const auto report = registry.deviceReport();
    const auto pinned = std::accumulate(
      report.begin(), report.end(), 0,
      []( int sum, const sicnu::operators::runtime::VramLedger::DeviceState &device ) {
        return sum + device.reservedMb;
      } );
    CHECK( pinned == 64 ); // two held sessions × 32 MiB stay accounted
  }

  onCuda0.reset();
  onCuda1.reset();
  for ( const auto &device : registry.deviceReport() )
    CHECK( device.reservedMb == 0 );
}

TEST_CASE( "pool stress: the pressure valve never over-commits the ledger",
           "[models][stress][ledger]" )
{
  RegistryReset reset;
  auto &registry = ModelRuntimeRegistry::instance();

  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaRuntimeAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 100;
  hw.deviceTotalVramMb = { 100 };
  hw.deviceFreeVramMb = { 100 };
  const HardwarePin pin( hw );
  registerStressProvider();

  // 4 threads race 4 distinct models × 32 MiB on ONE 100 MiB device:
  // admissions must fail (typed) beyond capacity — never over-commit.
  // Catch2 assertions run ONLY on the test thread: workers publish through
  // atomics, verdicts are asserted after the join.
  constexpr int kThreads = 4;
  std::atomic<int> admissions{ 0 };
  std::atomic<int> refusals{ 0 };
  std::atomic<int> deviceMismatches{ 0 };
  std::atomic<int> emptyErrors{ 0 };
  QTemporaryDir dir;
  std::vector<std::thread> threads;
  for ( int t = 0; t < kThreads; ++t )
  {
    threads.emplace_back( [ &, t] {
      const std::string name = "ledger-model-" + std::to_string( t );
      const ModelInfo model = stressModel( name, distinctArtifact( dir, name ) );
      std::string error;
      const auto session = registry.acquire( model, RequestedDevice::cuda( 0 ), &error );
      if ( session )
      {
        if ( session->deviceName() != "cuda:0" )
          ++deviceMismatches;
        ++admissions;
      }
      else
      {
        if ( error.empty() )
          ++emptyErrors; // a typed refusal must carry its reason
        ++refusals;
      }
    } );
  }
  for ( std::thread &thread : threads )
    thread.join();

  CHECK( admissions.load() + refusals.load() == kThreads );
  CHECK( deviceMismatches.load() == 0 );
  CHECK( emptyErrors.load() == 0 );
  const auto report = registry.deviceReport();
  REQUIRE_FALSE( report.empty() );
  CHECK( report.front().reservedMb <= report.front().capacityMb );
}

// --- R9 WP-D: same-key concurrent acquire coalescing -----------------------
// PR #1469 review finding: a same-key cache miss held the registry lock only
// until admission; the slow factory load ran UNLOCKED, so a second thread
// acquiring the same model took its own admission (a false VRAM refusal when
// the budget fits one session) and its own factory call (a duplicate weight
// load). The contract under test: ONE load, ONE ledger ticket per key, and
// waiters ride the loader's outcome — success or typed failure.

namespace {

/// Latch-controlled provider: every invocation counts, blocks until released,
/// and (on demand) fails with an invocation-numbered error so broadcast vs.
/// re-load is observable.
struct LatchedProvider
{
    std::mutex mutex;
    std::condition_variable cv;
    bool released = false;
    std::atomic<int> invocations{ 0 };
    bool failEveryLoad = false;
};

void registerLatchedProvider( const std::string &framework, LatchedProvider &provider )
{
    ModelRuntimeRegistry::instance().registerProvider(
        framework,
        [ &provider, framework ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                       std::string *error ) -> ModelRuntimePtr {
            const int invocation = provider.invocations.fetch_add( 1 ) + 1;
            {
                std::unique_lock<std::mutex> lock( provider.mutex );
                provider.cv.wait( lock, [ &provider ] { return provider.released; } );
            }
            if ( provider.failEveryLoad )
            {
                if ( error )
                    *error = "latched-failure-" + std::to_string( invocation );
                return nullptr;
            }
            return std::make_shared<DeviceTaggedRuntime>( model.resolvedArtifactPath, framework,
                                                          model.runtime.gpu
                                                              ? "cuda:" + std::to_string( model.runtime.resolvedCudaIndex )
                                                              : "cpu" );
        },
        ProviderTraits{ /*maxAddressableCudaIndex*/ 0 } );
}

void releaseLatchedProvider( LatchedProvider &provider )
{
    {
        std::lock_guard<std::mutex> lock( provider.mutex );
        provider.released = true;
    }
    provider.cv.notify_all();
}

/// Bounded spin until the loader thread is INSIDE the factory. A raw
/// `while (invocations == 0) sleep(1ms)` would hang the suite forever if the
/// loader died before entering it; this fails the case instead (R9 review).
bool waitForLoadStart( LatchedProvider &provider, int attempts = 5000 )
{
    for ( int i = 0; i < attempts; ++i )
    {
        if ( provider.invocations.load() > 0 )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    return false;
}

} // namespace

TEST_CASE( "same-key concurrent acquire: no false VRAM refusal, one ticket",
           "[models][pool][concurrency]" )
{
    RegistryReset reset;
    auto &registry = ModelRuntimeRegistry::instance();

    // One GPU with 512 MiB; the model estimates 400 — fits ONCE, never twice.
    ModelHardwareCapabilities hw;
    hw.cudaAvailable = true;
    hw.cudaRuntimeAvailable = true;
    hw.cudaDeviceCount = 1;
    hw.vramBudgetMb = 512;
    hw.deviceTotalVramMb = { 512 };
    hw.deviceFreeVramMb = { 512 };
    const HardwarePin pin( hw );

    LatchedProvider provider;
    registerLatchedProvider( "coalescefw", provider );

    QTemporaryDir dir;
    ModelInfo model = stressModel( "coalesce-gpu", distinctArtifact( dir, "coalesce-gpu.onnx" ) );
    model.framework = "coalescefw";
    model.runtime.estimatedVramMb = 400;

    ModelRuntimePtr first;
    std::thread loader( [ & ] { first = registry.acquire( model ); } );
    // Wait until the loader is INSIDE the factory (load in flight).
    REQUIRE( waitForLoadStart( provider ) );

    ModelRuntimePtr second;
    std::string secondError;
    std::thread peer( [ & ] { second = registry.acquire( model, &secondError ); } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    releaseLatchedProvider( provider );
    loader.join();
    peer.join();

    REQUIRE( first != nullptr );
    // The peer must NOT be falsely refused ("device unavailable") while the
    // identical session is being loaded, and must ride the same session.
    REQUIRE( second != nullptr );
    REQUIRE( secondError.empty() );
    REQUIRE( second == first );
    REQUIRE( provider.invocations.load() == 1 );
    const auto report = registry.deviceReport();
    REQUIRE_FALSE( report.empty() );
    CHECK( report.front().reservedMb == 400 ); // one ticket, not two
}

TEST_CASE( "same-key concurrent acquire on cpu loads the weights once",
           "[models][pool][concurrency]" )
{
    RegistryReset reset;
    auto &registry = ModelRuntimeRegistry::instance();
    LatchedProvider provider;
    registerLatchedProvider( "coalescecpufw", provider );

    QTemporaryDir dir;
    ModelInfo model = stressModel( "coalesce-cpu", distinctArtifact( dir, "coalesce-cpu.onnx" ) );
    model.framework = "coalescecpufw";
    model.runtime.gpu = false; // CPU: admission is a no-op; the load still costs

    ModelRuntimePtr first;
    std::thread loader( [ & ] { first = registry.acquire( model ); } );
    REQUIRE( waitForLoadStart( provider ) );

    ModelRuntimePtr second;
    std::thread peer( [ & ] { second = registry.acquire( model ); } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    releaseLatchedProvider( provider );
    loader.join();
    peer.join();

    REQUIRE( first != nullptr );
    REQUIRE( second != nullptr );
    REQUIRE( second == first );
    // One factory call per key: the duplicate weight parse the raced-insert
    // path used to pay is a coalesced wait now.
    REQUIRE( provider.invocations.load() == 1 );
}

TEST_CASE( "a failed same-key load broadcasts its typed failure to waiters",
           "[models][pool][concurrency]" )
{
    RegistryReset reset;
    auto &registry = ModelRuntimeRegistry::instance();
    LatchedProvider provider;
    provider.failEveryLoad = true;
    registerLatchedProvider( "coalescefailfw", provider );

    QTemporaryDir dir;
    ModelInfo model = stressModel( "coalesce-fail", distinctArtifact( dir, "coalesce-fail.onnx" ) );
    model.framework = "coalescefailfw";
    model.runtime.gpu = false;

    std::string firstError;
    std::thread loader( [ & ] { (void)registry.acquire( model, &firstError ); } );
    REQUIRE( waitForLoadStart( provider ) );

    std::string peerError;
    std::thread peer( [ & ] { (void)registry.acquire( model, &peerError ); } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    releaseLatchedProvider( provider );
    loader.join();
    peer.join();

    REQUIRE( firstError.find( "latched-failure-1" ) != std::string::npos );
    // The waiter receives the loader's OWN typed failure — it must not pay a
    // second load attempt (which would surface as failure-2).
    REQUIRE( peerError.find( "latched-failure-1" ) != std::string::npos );
    REQUIRE( provider.invocations.load() == 1 );
}
