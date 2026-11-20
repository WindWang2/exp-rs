// tests/test_r35_t02_ensemble_acquire_cancel.cpp — R35-T02 WP18: a parent
// cancellation must reach ensemble member session ACQUISITION, not just the
// forward stage.
//
// Pre-fix acquireEnsembleSessions (model_ensemble.cpp) called
// registry.acquireWithFallback without the parent's cancel predicate — the
// single-model service already passed it (model_execution_service.cpp), so an
// ensemble whose member waited on a peer's in-flight load (or between
// fallback candidates / members) ignored the parent's cancellation until the
// flight resolved by itself. This case pins the contract with the REAL
// registry seam and fixed barriers:
//
//   member-b (instant scale)  → loads first; observable hook that the run
//                               reached the acquisition stage;
//   member-a (blocking load)  → a pre-existing loader thread parks the flight
//                               inside an uninterruptible factory, the
//                               ensemble member becomes a WAITER on it;
//   member-c (instant scale)  → ordered AFTER the blocked member: must never
//                               load once the parent cancels.
//
// The parent cancels while member-a's flight stays blocked: the ensemble run
// must exit with the typed Cancelled error within a bounded window (the
// registry polls cancellation at 100 ms granularity), member-c must never
// start, and — isolation — a concurrent LEGAL waiter plus a later retry on
// the same identity must still get the session once the loader is released.
// The blocking factory lane is honestly uninterruptible: what is cancellable
// is the WAIT, never the third-party synchronous factory call itself.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "synthetic_raster_builder.h"

#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using namespace sicnu::operators::runtime;

/// The registry is a process-global cache; start and end the case clean.
struct RegistryReset
{
  RegistryReset()
  {
    ModelRuntimeRegistry::instance().releaseAll();
    ModelRuntimeRegistry::instance().resetLoadCount();
    ModelRuntimeRegistry::instance().setMaxCachedSessions( 16 );
    ModelRuntimeRegistry::instance().setIdleEvictionMs( 0 );
  }
  ~RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
};

/// Minimal deterministic member: NCHW blob × scale. Forward is never reached
/// in this case (the run is cancelled during acquisition), so the session only
/// needs to load and answer identity questions.
class ScaleSession final : public IModelRuntime
{
  public:
    ScaleSession( std::string artifact, std::string framework, double scale )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_scale( scale )
    {
    }

    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "r35t02_scale_backend"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      cv::Mat out = blob.clone();
      out.convertTo( out, CV_32F, m_scale );
      return out;
    }

  private:
    std::string m_artifact;
    std::string m_framework;
    double m_scale;
};

/// Fixed barrier for the blocking factory: entered is observable, release is
/// decided by the test. All releases happen on the main thread; a scope guard
/// opens the gate even if an assertion fails mid-case so no thread leaks past
/// the test.
struct FactoryGate
{
  std::promise<void> releasePromise;
  std::shared_future<void> release = releasePromise.get_future().share();
  std::atomic<bool> entered{ false };

  void open()
  {
    if ( !opened.exchange( true ) )
      releasePromise.set_value();
  }

  std::atomic<bool> opened{ false };
};

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

void registerMember( const std::string &name, const std::string &framework,
                     const QTemporaryDir &dir )
{
  Json::Value json( Json::objectValue );
  json["name"] = name;
  json["task"] = "segmentation";
  json["framework"] = framework;
  json["artifact"]["path"] = identityModelPath().toStdString();
  json["tiling"]["tile_size"] = 16;
  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ),
    dir.filePath( QString::fromStdString( name ) + QStringLiteral( "/model.json" ) ).toStdString(),
    &error );
  if ( !ok )
    FAIL( "member '" + name + "' rejected: " + error );
}

/// Members registered in EXPLICIT order: b (instant), a (blocked), c (never).
void registerOrderedEnsemble( const QTemporaryDir &dir, const std::string &ensembleName )
{
  Json::Value json( Json::objectValue );
  json["name"] = ensembleName;
  json["task"] = "segmentation";
  json["framework"] = "onnx";
  Json::Value &ensemble = json["ensemble"] = Json::Value( Json::objectValue );
  Json::Value members( Json::arrayValue );
  for ( const char *name : { "member-b", "member-a", "member-c" } )
  {
    Json::Value entry( Json::objectValue );
    entry["model"] = name;
    entry["weight"] = 1.0;
    members.append( entry );
  }
  ensemble["members"] = members;
  ensemble["max_concurrent_members"] = 2;
  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ),
    dir.filePath( QString::fromStdString( ensembleName ) + QStringLiteral( "/model.json" ) )
      .toStdString(),
    &error );
  if ( !ok )
    FAIL( "ensemble '" + ensembleName + "' rejected: " + error );
}

/// Poll-based bounded wait (main thread only; no Catch assertions off-thread).
bool waitUntil( const std::function<bool()> &pred, int budgetMs )
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( budgetMs );
  while ( !pred() )
  {
    if ( std::chrono::steady_clock::now() > deadline )
      return false;
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }
  return true;
}

/// Opens the gate and joins every worker thread on ANY exit (a failed
/// REQUIRE throws through — without this guard the blocked factory would
/// park the threads past the case and hang the test binary at teardown).
struct JoinGuard
{
  FactoryGate &gate;
  std::vector<std::thread> threads;

  ~JoinGuard()
  {
    gate.open();
    for ( std::thread &thread : threads )
      if ( thread.joinable() )
        thread.join();
  }
};

} // namespace

TEST_CASE( "R35-T02 WP18: parent cancellation exits ensemble member acquisition "
           "within a bound",
           "[models][ensemble][r35t02][cancel]" )
{
  RegistryReset reset;
  FactoryGate gateA;
  std::atomic<int> scaleBLoads{ 0 };
  std::atomic<int> scaleCLoads{ 0 };
  std::atomic<bool> cancelFlag{ false };

  auto &registry = ModelRuntimeRegistry::instance();
  registry.registerProvider(
    "r35t02-blockfw",
    [ &gateA ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                std::string *error ) -> ModelRuntimePtr {
      if ( model.resolvedArtifactPath.empty() )
      {
        if ( error )
          *error = "no resolved artifact";
        return nullptr;
      }
      gateA.entered.store( true );
      // Deliberately uninterruptible factory (WP18 honesty contract): the
      // CANCELLED thing is the registry WAIT, never this call.
      gateA.release.wait();
      return std::make_shared<ScaleSession>( model.resolvedArtifactPath, "r35t02-blockfw", 1.0 );
    } );
  registry.registerProvider(
    "r35t02-scalefw-b",
    [ &scaleBLoads ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                      std::string * ) -> ModelRuntimePtr {
      scaleBLoads.fetch_add( 1 );
      return std::make_shared<ScaleSession>( model.resolvedArtifactPath, "r35t02-scalefw-b", 2.0 );
    } );
  registry.registerProvider(
    "r35t02-scalefw-c",
    [ &scaleCLoads ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                      std::string * ) -> ModelRuntimePtr {
      scaleCLoads.fetch_add( 1 );
      return std::make_shared<ScaleSession>( model.resolvedArtifactPath, "r35t02-scalefw-c", 3.0 );
    } );

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  registerMember( "member-a", "r35t02-blockfw", dir );
  registerMember( "member-b", "r35t02-scalefw-b", dir );
  registerMember( "member-c", "r35t02-scalefw-c", dir );
  registerOrderedEnsemble( dir, "ens-r35t02-acq" );

  const QString input = [ & ] {
    const QString path = dir.filePath( QStringLiteral( "input.tif" ) );
    sicnu::testing::RsSyntheticRasterBuilder builder( 64, 64, 2, GDT_Float32 );
    for ( int b = 1; b <= 2; ++b )
      builder.withConstantValue( b, 10.0f );
    builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( path );
    return path;
  }();

  RSOperatorContext context;
  context.setCancelFlag( &cancelFlag );

  // The member ModelInfo EXACTLY as the ensemble resolves it (same identity
  // → same in-flight key: framework | device | digest).
  std::string resolveError;
  const ModelInfo memberA = resolveModelReference( "member-a", &resolveError );
  REQUIRE( memberA.readiness == ModelReadiness::Ready );
  REQUIRE( memberA.framework == "r35t02-blockfw" );

  // Results captured off-thread; asserted on the main thread only.
  ModelRuntimePtr loaderSession;
  ModelRuntimePtr peerSession;
  std::atomic<bool> ensembleDone{ false };
  std::string ensembleOutcome = "running";
  // Declared AFTER the result locals, joined FIRST on any exit (its
  // destructor opens the gate and joins the threads below).
  JoinGuard guard{ gateA, {} };

  // (1) Pre-existing loader: owns member-a's flight, parks inside the factory.
  guard.threads.emplace_back( [ & ] {
    std::string error;
    loaderSession = registry.acquire( memberA, RequestedDevice::cpu(), &error );
  } );
  // (2) A concurrent LEGAL waiter on the same flight (no cancel predicate):
  //     the ensemble's cancellation must never disturb it.
  guard.threads.emplace_back( [ & ] {
    std::string error;
    peerSession = registry.acquire( memberA, RequestedDevice::cpu(), &error );
  } );
  // (3) The ensemble run (member order b → a → c).
  guard.threads.emplace_back( [ & ] {
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "out.tif" ) ).toStdString();
    request.modelReference = "ens-r35t02-acq";
    try
    {
      runModelInference( request, context );
      ensembleOutcome = "ok";
    }
    catch ( const RSOperatorError &error )
    {
      ensembleOutcome = error.code() == sicnu::operators::ErrorCode::Cancelled
                          ? "cancelled"
                          : "error: " + error.message();
    }
    catch ( const std::exception &error )
    {
      ensembleOutcome = std::string( "exception: " ) + error.what();
    }
    ensembleDone.store( true );
  } );

  // Arm: the blocked factory is entered AND member-b already loaded — the run
  // has provably reached the member-acquisition stage.
  REQUIRE( waitUntil( [ & ] { return gateA.entered.load() && scaleBLoads.load() >= 1; },
                      10000 ) );
  // Let the ensemble register as a waiter on member-a's flight (its acquire
  // follows member-b's in member order).
  std::this_thread::sleep_for( std::chrono::milliseconds( 150 ) );
  cancelFlag.store( true );

  // THE WP18 contract: bounded exit while the flight stays blocked. The
  // registry polls cancellation at 100 ms granularity, so 5 s is generous.
  // Pre-fix the ensemble waiter ignored the cancel and stayed parked until
  // the loader was released — this CHECK is the RED line.
  CHECK( waitUntil( [ & ] { return ensembleDone.load(); }, 5000 ) );
  // And member-c — ordered after the blocked member — never starts loading.
  CHECK( scaleCLoads.load() == 0 );

  // Release the loader in every outcome (RED included) so no thread leaks.
  gateA.open();
  for ( std::thread &thread : guard.threads )
    if ( thread.joinable() )
      thread.join();
  guard.threads.clear();

  INFO( "ensemble outcome: " << ensembleOutcome );
  CHECK( ensembleOutcome == "cancelled" );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "out.tif" ) ) ) );

  // Isolation: the legal waiter rode the same flight the ensemble cancelled
  // on and still received the session the loader published...
  CHECK( peerSession != nullptr );
  CHECK( loaderSession != nullptr );
  // ...and a subsequent retry of the same identity keeps working (cache hit).
  std::string retryError;
  const auto retry = registry.acquire( memberA, RequestedDevice::cpu(), &retryError );
  INFO( "retry error: " << retryError );
  CHECK( retry != nullptr );
}
