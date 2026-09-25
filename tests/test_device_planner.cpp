// tests/test_device_planner.cpp — Platform 7.0 deterministic GPU device
// planner: DeviceInventory, VramLedger admission, ledger-aware device
// resolution, registry admission + memory-pressure eviction, and concurrent
// acquire/release under a bounded ledger. Everything runs on GPU-less hosts
// through the injectable inventory / hardware-override seams.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/runtime/device_planner.h"
#include "operators/runtime/model_runtime.h"

#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelInfo;

class FakeSession final : public IModelRuntime
{
  public:
    explicit FakeSession( std::string device )
        : m_device( std::move( device ) )
    {
    }
    std::string framework() const override { return "planner7"; }
    std::string backendName() const override { return "planner-fake"; }
    std::string deviceName() const override { return m_device; }
    std::string artifactPath() const override { return m_path; }
    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }
    std::string m_device;
    std::string m_path;
};

/// Scripted crash-recovery session: after @p forwardsUntilDeath forwards it
/// dies for good — the model of an external worker whose restart budget is
/// exhausted (Track 13). permanentlyUnavailable() is what the registry
/// consults to recycle the cached corpse instead of serving it forever.
class ZombieSession final : public IModelRuntime
{
  public:
    ZombieSession( std::string device, int forwardsUntilDeath,
                   std::shared_ptr<std::atomic<int>> constructions )
        : m_device( std::move( device ) ),
          m_limit( forwardsUntilDeath ),
          m_constructions( std::move( constructions ) )
    {
      m_constructions->fetch_add( 1 );
    }
    std::string framework() const override { return "planner7"; }
    std::string backendName() const override { return "zombie-fake"; }
    std::string deviceName() const override { return m_device; }
    std::string artifactPath() const override { return m_path; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      if ( m_dead.load() )
        throw std::runtime_error( "provider crashed: restart budget is exhausted" );
      cv::Mat out = blob.clone(); // the last forward COMPLETES, then the worker dies
      if ( m_forwards.fetch_add( 1 ) + 1 >= m_limit )
        m_dead.store( true );
      return out;
    }
    bool permanentlyUnavailable() const override { return m_dead.load(); }

    std::string m_device;
    std::string m_path;
    int m_limit = 1;
    std::atomic<int> m_forwards{ 0 };
    std::atomic<bool> m_dead{ false };
    std::shared_ptr<std::atomic<int>> m_constructions;
};

/// Builds a GPU model contract with its own artifact file (digest identity).
ModelInfo makeModel( const QTemporaryDir &dir, const std::string &name, int estimatedVramMb,
                     bool cpuFallback = false )
{
  ModelInfo model;
  model.name = name;
  model.id = name;
  model.framework = "planner7";
  model.runtime.gpu = true;
  model.runtime.estimatedVramMb = estimatedVramMb;
  model.runtime.cpuFallback = cpuFallback;
  const QString path = dir.filePath( QString::fromStdString( name + ".bin" ) );
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  file.write( QByteArray( "weights-" ) + QByteArray::number( static_cast<qulonglong>( estimatedVramMb ) ) );
  file.close();
  model.resolvedArtifactPath = path.toStdString();
  model.contentDigest = "digest-" + name;
  return model;
}

struct RegistryGuard
{
    RegistryGuard()
    {
      auto &registry = ModelRuntimeRegistry::instance();
      savedHardware = registry.hardware();
      registry.setMaxCachedSessions( 8 );
      registry.setIdleEvictionMs( 0 );
      registry.vramLedger().reset();
      registry.releaseAll();
      registry.resetLoadCount();
    }
    ~RegistryGuard()
    {
      auto &registry = ModelRuntimeRegistry::instance();
      registry.releaseAll();
      registry.vramLedger().reset();
      registry.setHardwareForTest( std::nullopt );
      registry.setMaxCachedSessions( 2 );
    }
    ModelHardwareCapabilities savedHardware;
};

} // namespace

TEST_CASE( "VramLedger admission is deterministic and idempotent", "[models][planner]" )
{
  VramLedger ledger;
  ledger.setCapacity( 0, 100 );
  ledger.setCapacity( 1, 40 );

  std::string why;
  CHECK( ledger.tryReserve( 0, 60, "session-a", &why ) );
  CHECK( ledger.freeMb( 0 ) == 40 );
  CHECK_FALSE( ledger.tryReserve( 1, 60, "session-b", &why ) );
  CHECK( why.find( "cuda:1" ) != std::string::npos );
  CHECK( why.find( "free of" ) != std::string::npos );

  // Re-reserving the same holder replaces, never double-books.
  CHECK( ledger.tryReserve( 0, 30, "session-a" ) );
  CHECK( ledger.reservedMb( 0 ) == 30 );

  // Release is idempotent; unknown holders are no-ops.
  ledger.release( 0, 30, "session-a" );
  ledger.release( 0, 30, "session-a" );
  ledger.release( 0, 30, "ghost" );
  CHECK( ledger.reservedMb( 0 ) == 0 );

  // Unknown estimates and unenforced capacities admit without accounting.
  CHECK( ledger.tryReserve( 5, 0, "session-c" ) );
  CHECK( ledger.reservedMb( 5 ) == 0 );
}

TEST_CASE( "ledger-aware resolveDevice picks the lowest FITTING device", "[models][planner]" )
{
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 3;
  hw.vramBudgetMb = 64;

  // -1 = unenforced. cuda:0 full, cuda:1 fits, cuda:2 unenforced.
  const std::vector<int> freeVram = { 0, 32, -1 };

  ResolvedDevice device;
  std::string why;
  REQUIRE( resolveDevice( RequestedDevice::autoDetect(), hw, true, 32, /*maxIdx*/ 2,
                          /*fallback*/ false, freeVram, &device, &why ) );
  CHECK( device.gpu );
  CHECK( device.cudaIndex == 1 ); // lowest index that FITS, not trivially 0

  // Nothing fits and no fallback is a typed refusal.
  const std::vector<int> allFull = { 0, 0, 0 };
  CHECK_FALSE( resolveDevice( RequestedDevice::autoDetect(), hw, true, 32, 2, false, allFull,
                              &device, &why ) );
  CHECK( why.find( "no fitting CUDA device" ) != std::string::npos );

  // Explicit cuda:N must fit its own card.
  const std::vector<int> onlyTwo = { 64, 0, 64 };
  REQUIRE( resolveDevice( RequestedDevice::cuda( 2 ), hw, true, 32, 2, false, onlyTwo, &device,
                          &why ) );
  CHECK( device.cudaIndex == 2 );
  CHECK_FALSE( resolveDevice( RequestedDevice::cuda( 1 ), hw, true, 32, 2, false, onlyTwo,
                              &device, &why ) );
  CHECK( why.find( "cpu_fallback" ) != std::string::npos );

  // Empty free list = no ledger: the historical contract holds (auto → cuda:0).
  REQUIRE( resolveDevice( RequestedDevice::autoDetect(), hw, true, 32, 2, false, {}, &device,
                          &why ) );
  CHECK( device.cudaIndex == 0 );
}

TEST_CASE( "registry admission reserves the ledger and evicts under pressure", "[models][planner]" )
{
  QTemporaryDir dir;
  RegistryGuard guard;
  auto &registry = ModelRuntimeRegistry::instance();
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 2;
  hw.vramBudgetMb = 100;
  registry.setHardwareForTest( hw );
  registry.registerProvider(
    "planner7",
    []( const ModelInfo &model, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      return std::make_shared<FakeSession>( "cuda" );
    },
    ProviderTraits{ /*maxAddressableCudaIndex*/ 1 } );
  registry.vramLedger().setCapacity( 0, 100 );
  registry.vramLedger().setCapacity( 1, 100 );

  const ModelInfo bigA = makeModel( dir, "big-a", 60 );
  const ModelInfo bigB = makeModel( dir, "big-b", 60 );

  std::string error;
  // Auto lands A on cuda:0 (lowest fitting: 100 free both sides).
  auto sessionA = registry.acquire( bigA, RequestedDevice::autoDetect(), &error );
  REQUIRE( sessionA );
  CHECK( registry.vramLedger().reservedMb( 0 ) == 60 );

  // B does not fit cuda:0 (60+60 > 100) → auto deterministically picks cuda:1.
  auto sessionB = registry.acquire( bigB, RequestedDevice::autoDetect(), &error );
  REQUIRE( sessionB );
  CHECK( registry.vramLedger().reservedMb( 1 ) == 60 );

  // #1160: reservations follow the SESSION, not the cache entry. While the
  // caller still holds sessionA, an explicit cuda:0 acquisition of B2
  // cannot admit — evicting A's cache entry frees nothing (the model is
  // still loaded and physically occupying the card), and the ledger must
  // refuse rather than over-commit. The typed refusal is the honest answer.
  const ModelInfo bigB2 = makeModel( dir, "big-b2", 60 );
  CHECK_FALSE( registry.acquire( bigB2, RequestedDevice::cuda( 0 ), &error ) );
  CHECK( error.find( "device unavailable" ) != std::string::npos );
  CHECK( registry.vramLedger().reservedMb( 0 ) == 60 ); // A's occupancy counted

  // Dropping the last reference releases the reservation (session lifetime
  // is the reservation lifetime) — the retry then evicts the idle entry
  // and admits.
  sessionA.reset();
  CHECK( registry.vramLedger().reservedMb( 0 ) == 0 );
  auto sessionC = registry.acquire( bigB2, RequestedDevice::cuda( 0 ), &error );
  REQUIRE( sessionC );
  CHECK( registry.vramLedger().reservedMb( 0 ) == 60 );
  CHECK( registry.cachedSessionCount() == 2 ); // A evicted, B(cuda:1)+C(cuda:0) cached
  CHECK( registry.poolStats().evictions >= 1 );

  sessionB.reset();
  sessionC.reset();
  registry.releaseAll();
  CHECK( registry.vramLedger().reservedMb( 0 ) == 0 );
  CHECK( registry.vramLedger().reservedMb( 1 ) == 0 );
}

TEST_CASE( "#1160: an externally held session keeps its ledger reservation past eviction",
           "[models][planner][issue1160]" )
{
  // The ensemble pre-acquisition shape: more live member sessions than the
  // default pool (2). Evicting a member's CACHE entry while its session is
  // still held must NOT release the reservation — the GPU memory is still
  // occupied and a concurrent acquire must be refused against it.
  QTemporaryDir dir;
  RegistryGuard guard;
  auto &registry = ModelRuntimeRegistry::instance();
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 200;
  registry.setHardwareForTest( hw );
  // The ensemble pre-acquisition shape needs the PRODUCTION default pool
  // (2); RegistryGuard widens it to 8 for the other tests.
  registry.setMaxCachedSessions( 2 );
  registry.registerProvider(
    "planner7",
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      return std::make_shared<FakeSession>( "cuda" );
    },
    ProviderTraits{} );
  registry.vramLedger().setCapacity( 0, 200 );

  // Default pool size: 2 sessions.
  std::string error;
  const ModelInfo mA = makeModel( dir, "member-a", 40 );
  const ModelInfo mB = makeModel( dir, "member-b", 40 );
  const ModelInfo mC = makeModel( dir, "member-c", 40 );
  auto heldA = registry.acquire( mA, RequestedDevice::cuda( 0 ), &error );
  auto heldB = registry.acquire( mB, RequestedDevice::cuda( 0 ), &error );
  REQUIRE( heldA );
  REQUIRE( heldB );
  CHECK( registry.vramLedger().reservedMb( 0 ) == 80 );

  // Third member fits the ledger (120 of 200) but not the pool: the LRU
  // invariant drops A's ENTRY; the held session keeps its 40 MiB counted —
  // exactly the occupancy that must block a concurrent acquire.
  auto heldC = registry.acquire( mC, RequestedDevice::cuda( 0 ), &error );
  REQUIRE( heldC );
  CHECK( registry.cachedSessionCount() == 2 );
  CHECK( registry.vramLedger().reservedMb( 0 ) == 120 );

  // Concurrent acquire during member execution: 100 more never fits 200
  // with three live reservations — refused, never over-committed.
  const ModelInfo outsider = makeModel( dir, "outsider", 100 );
  CHECK_FALSE( registry.acquire( outsider, RequestedDevice::cuda( 0 ), &error ) );
  CHECK( error.find( "device unavailable" ) != std::string::npos );

  // Releasing a held session releases its reservation promptly.
  heldA.reset();
  CHECK( registry.vramLedger().reservedMb( 0 ) == 80 );
  heldB.reset();
  heldC.reset();
  registry.releaseAll();
  CHECK( registry.vramLedger().reservedMb( 0 ) == 0 );
}

TEST_CASE( "registry refusal is typed when pressure eviction cannot admit", "[models][planner]" )
{
  QTemporaryDir dir;
  RegistryGuard guard;
  auto &registry = ModelRuntimeRegistry::instance();
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 100;
  registry.setHardwareForTest( hw );
  registry.registerProvider(
    "planner7",
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      return std::make_shared<FakeSession>( "cuda" );
    },
    ProviderTraits{} );
  registry.vramLedger().setCapacity( 0, 100 );

  const ModelInfo big = makeModel( dir, "huge", 60, /*cpuFallback*/ false );
  const ModelInfo monster = makeModel( dir, "monster", 200, /*cpuFallback*/ false );

  std::string error;
  REQUIRE( registry.acquire( big, RequestedDevice::autoDetect(), &error ) );

  // Even after evicting every session on cuda:0, 200 MiB never fits 100 MiB:
  // a typed device-unavailable refusal, never a silent demotion.
  CHECK_FALSE( registry.acquire( monster, RequestedDevice::autoDetect(), &error ) );
  CHECK( error.find( "device unavailable" ) != std::string::npos );
  CHECK( classifyInferenceError( error ) == InferenceFailureKind::DeviceUnavailable );
}

TEST_CASE( "concurrent acquire/release respects the ledger bound", "[models][planner]" )
{
  QTemporaryDir dir;
  RegistryGuard guard;
  auto &registry = ModelRuntimeRegistry::instance();
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 100;
  registry.setHardwareForTest( hw );
  registry.registerProvider(
    "planner7",
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      return std::make_shared<FakeSession>( "cuda" );
    },
    ProviderTraits{} );
  registry.vramLedger().setCapacity( 0, 100 );
  registry.setMaxCachedSessions( 16 );

  std::atomic<bool> overCommitted{ false };
  std::atomic<int> admitted{ 0 };
  std::vector<std::thread> threads;
  for ( int t = 0; t < 4; ++t )
  {
    threads.emplace_back( [ &, t] {
      for ( int i = 0; i < 10; ++i )
      {
        const ModelInfo model = makeModel( dir, "conc-" + std::to_string( t ) + "-" +
                                                    std::to_string( i ), 40 );
        std::string error;
        if ( auto session = registry.acquire( model, RequestedDevice::autoDetect(), &error ) )
        {
          if ( registry.vramLedger().reservedMb( 0 ) > 100 )
            overCommitted.store( true );
          ++admitted;
          registry.release( "planner7", model.contentDigest );
        }
      }
    } );
  }
  for ( auto &thread : threads )
    thread.join();
  CHECK_FALSE( overCommitted.load() );
  CHECK( registry.vramLedger().reservedMb( 0 ) <= 100 );
}

TEST_CASE( "device inventory derives deterministically from hardware", "[models][planner]" )
{
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 2;
  hw.vramBudgetMb = 8192;
  const DeviceInventory inventory = DeviceInventory::fromHardware( hw );
  REQUIRE( inventory.cudaDevices().size() == 2 );
  CHECK( inventory.device( 0 )->vramCapacityMb == 8192 );
  CHECK( inventory.device( 1 )->name == "cuda:1" );
  CHECK( inventory.device( 2 ) == nullptr );

  ModelHardwareCapabilities noGpu;
  noGpu.cudaAvailable = false;
  CHECK( DeviceInventory::fromHardware( noGpu ).empty() );
}


TEST_CASE( "ledger capacities seed from the inventory (no manual setup)", "[models][planner]" )
{
  QTemporaryDir dir;
  RegistryGuard guard;
  auto &registry = ModelRuntimeRegistry::instance();
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 100;
  registry.setHardwareForTest( hw );
  registry.registerProvider(
    "planner7",
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      return std::make_shared<FakeSession>( "cuda" );
    },
    ProviderTraits{} );
  // NO manual vramLedger().setCapacity: acquire must seed capacities itself,
  // otherwise freeMb would read 0 and every GPU request would silently
  // demote to cpu.
  const ModelInfo model = makeModel( dir, "seeded", 60 );
  std::string error;
  const auto session = registry.acquire( model, RequestedDevice::autoDetect(), &error );
  REQUIRE( session );
  CHECK( registry.vramLedger().reservedMb( 0 ) == 60 );
}

TEST_CASE( "a throwing factory leaves no residual reservation", "[models][planner]" )
{
  QTemporaryDir dir;
  RegistryGuard guard;
  auto &registry = ModelRuntimeRegistry::instance();
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 100;
  registry.setHardwareForTest( hw );
  int calls = 0;
  registry.registerProvider(
    "planner7",
    [ &calls ]( const ModelInfo &, const ModelHardwareCapabilities &, std::string * )
      -> ModelRuntimePtr {
      if ( ++calls == 1 )
        throw std::runtime_error( "factory exploded (bad_alloc style)" );
      return std::make_shared<FakeSession>( "cuda" );
    },
    ProviderTraits{} );
  registry.vramLedger().setCapacity( 0, 100 );

  const ModelInfo boom = makeModel( dir, "boom", 60 );
  std::string error;
  REQUIRE_THROWS_AS( registry.acquire( boom, RequestedDevice::autoDetect(), &error ),
                     std::runtime_error );
  // The reservation MUST be gone: the retry (same identity) fits and runs.
  CHECK( registry.vramLedger().reservedMb( 0 ) == 0 );
  const auto session = registry.acquire( boom, RequestedDevice::autoDetect(), &error );
  REQUIRE( session );
  CHECK( registry.vramLedger().reservedMb( 0 ) == 60 );
}

TEST_CASE( "a permanently dead session is recycled by the next acquire and the "
           "ledger never double-books the replacement",
           "[models][planner][crash-recovery]" )
{
  QTemporaryDir dir;
  RegistryGuard guard;
  auto &registry = ModelRuntimeRegistry::instance();
  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 100;
  registry.setHardwareForTest( hw );
  registry.vramLedger().setCapacity( 0, 100 );

  auto constructions = std::make_shared<std::atomic<int>>( 0 );
  // Every constructed session dies for good after its FIRST forward — the
  // "worker crashed, restart budget exhausted" model.
  registry.registerProvider(
    "planner7",
    [ constructions ]( const ModelInfo &, const ModelHardwareCapabilities &,
                       std::string * ) -> ModelRuntimePtr {
      return std::make_shared<ZombieSession>( "cuda", 1, constructions );
    },
    ProviderTraits{ /*maxAddressableCudaIndex*/ 1 } );

  const ModelInfo model = makeModel( dir, "zombie-model", 32 );

  std::string error;
  auto first = registry.acquire( model, RequestedDevice::cuda( 0 ), &error );
  REQUIRE( first );
  CHECK( constructions->load() == 1 );
  CHECK( registry.vramLedger().reservedMb( 0 ) == 32 );
  REQUIRE_NOTHROW( static_cast<void>( first->infer( cv::Mat() ) ) );

  // The forward killed it: every use of the cached session now throws —
  // the pre-recycling registry kept handing this corpse out forever.
  REQUIRE_THROWS_AS( first->infer( cv::Mat() ), std::runtime_error );

  // Drop the caller's reference FIRST (the production shape: the request
  // that killed the worker already released its session). The recycling
  // acquire must re-book the ledger for the replacement — a recycle that
  // lets the corpse's #1160 release erase the replacement's holder entry
  // leaves reservedMb at 0 while the fresh session is physically resident
  // (an over-admission hole), which this ordering catches.
  first.reset();
  CHECK( registry.cachedSessionCount() == 1 ); // the corpse is still cached

  auto second = registry.acquire( model, RequestedDevice::cuda( 0 ), &error );
  REQUIRE( second );
  INFO( "error: " << error );
  CHECK( constructions->load() == 2 ); // the recycle rebuilt the session
  CHECK( second != first );
  REQUIRE_NOTHROW( static_cast<void>( second->infer( cv::Mat() ) ) );

  // The replacement stays booked exactly once while it lives.
  CHECK( registry.vramLedger().reservedMb( 0 ) == 32 );
  CHECK( registry.cachedSessionCount() == 1 );

  second.reset();
  registry.releaseAll();
  CHECK( registry.vramLedger().reservedMb( 0 ) == 0 );
}

TEST_CASE( "healthy cached sessions are never recycled by the crash-recovery path",
           "[models][planner][crash-recovery]" )
{
  QTemporaryDir dir;
  RegistryGuard guard;
  auto &registry = ModelRuntimeRegistry::instance();
  auto constructions = std::make_shared<std::atomic<int>>( 0 );
  registry.registerProvider(
    "planner7",
    [ constructions ]( const ModelInfo &, const ModelHardwareCapabilities &,
                       std::string * ) -> ModelRuntimePtr {
      return std::make_shared<ZombieSession>( "cpu", /*forwardsUntilDeath*/ 1000,
                                              constructions );
    },
    ProviderTraits{} );

  const ModelInfo model = makeModel( dir, "healthy-model", 0 );
  std::string error;
  auto a = registry.acquire( model, RequestedDevice::cpu(), &error );
  REQUIRE( a );
  auto b = registry.acquire( model, RequestedDevice::cpu(), &error );
  REQUIRE( b );
  CHECK( constructions->load() == 1 ); // one cache hit, no rebuild
  CHECK( b == a );
}
