// tests/test_r35_t02_python_worker_deadline.cpp — R35-T02 WP01: the python
// worker read deadline is an ABSOLUTE bound on one wire frame exchange.
//
// Pre-fix readLine (python_worker_provider.cpp) only checked its deadline on
// the no-data path: a worker that kept a trickle of newline-free bytes
// arriving within every 100 ms poll window could bypass the deadline until the
// max_body_mb cap, and the clock source was wall time. These cases drive REAL
// out-of-process stdlib workers (tests/data/py_worker_*.py):
//   - py_worker_trickle.py        → RED case: byte trickle must hit the
//                                   deadline, not the body cap (bounded 8 s
//                                   trickle keeps the RED run non-hanging);
//   - py_worker_segmented.py      → a legal slow segmented frame INSIDE the
//                                   deadline still succeeds (partial-frame
//                                   semantics preserved);
//   - py_worker_silent.py         → complete silence fails at the deadline;
//   - py_worker_stderr_chatter.py → stderr activity with silent stdout
//                                   neither stalls nor extends the deadline.
// After a deadline failure the dead session is never reused (fail-closed),
// and a fresh legal session keeps serving (registry not poisoned).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/python_worker_provider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <chrono>
#include <string>
#include <vector>

namespace
{

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;

/// QProcess needs a QCoreApplication; Catch2 owns main, so create it lazily.
QCoreApplication &ensureApp()
{
  static int argc = 1;
  static char name[] = "test_r35_t02_python_worker_deadline";
  static char *argv[] = { name, nullptr };
  static QCoreApplication app( argc, argv );
  return app;
}

/// The registry is a process-global cache of live worker processes; every
/// case starts and ends clean so a dead session from one case can never be
/// served to another (same pattern as test_ensemble_detection.cpp).
struct RegistryReset
{
  RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
  ~RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
};

std::string dataDir()
{
  return ( QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data" ) ).toStdString();
}

ModelInfo makeWorkerModel( const QTemporaryDir &dir, const std::string &workerScript,
                           const std::string &weightsName, int timeoutMs )
{
  const QString weights = dir.filePath( QString::fromStdString( weightsName ) );
  QFile file( weights );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  // Unique bytes per case: the registry shares sessions by content digest.
  file.write( QByteArray( "r35t02-deadline-weights:" )
              + QString::fromStdString( workerScript ).toUtf8()
              + QByteArray( ":" ) + QString::fromStdString( weightsName ).toUtf8() );
  file.close();

  ModelInfo model;
  model.name = "r35t02-deadline";
  model.framework = "python";
  model.readiness = ModelReadiness::Ready;
  model.resolvedArtifactPath = weights.toStdString();
  model.runtime.provider.workerScript = workerScript;
  model.runtime.provider.timeoutMs = timeoutMs;
  // Anchor for relative worker scripts; ours are absolute, but the manifest
  // directory still fences them (resolveModelWorkerScript).
  model.sourceManifest = ( QFileInfo( __FILE__ ).absolutePath()
                           + QStringLiteral( "/data/manifest.json" ) )
                           .toStdString();
  return model;
}

std::vector<NamedTensor> scalarInput( const char *name, float value )
{
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ name, TensorBlob::fromFloat32( { 1, 1, 1, 1 }, &value, 1 ) } );
  return inputs;
}

} // namespace

TEST_CASE( "R35-T02 WP01: a trickling worker hits the absolute read deadline",
           "[models][python][r35t02]" )
{
  ( void )ensureApp();
  RegistryReset registryGuard;
  QTemporaryDir dir;
  const ModelInfo model = makeWorkerModel(
    dir, dataDir() + "/py_worker_trickle.py", "trickle.bin", 1200 );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  INFO( "acquire error: " << error );
  REQUIRE( session );

  const auto started = std::chrono::steady_clock::now();
  bool timedOut = false;
  std::string message;
  try
  {
    session->inferNamed( scalarInput( "x", 1.0f ), {} );
    FAIL( "expected the trickle to fail the forward" );
  }
  catch ( const std::exception &e )
  {
    message = e.what();
    timedOut = classifyInferenceError( message ) == InferenceFailureKind::Timeout;
  }
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started )
                           .count();
  INFO( "forward failed after " << elapsedMs << " ms: " << message );

  // The taxonomy: a LIVE worker that missed the deadline is a Timeout, and the
  // message pins the deadline that fired.
  CHECK( timedOut );
  CHECK_THAT( message, Catch::Matchers::ContainsSubstring( "did not answer within 1200 ms" ) );

  // THE WP01 contract: the deadline is absolute — the failure must land within
  // a bounded tolerance of 1200 ms. Pre-fix the trickle postponed the deadline
  // check to the first no-data poll (~8 s in, plus interpreter startup); the
  // bound below rejects that while tolerating CI scheduling noise. And it must
  // never fire before the deadline itself.
  CHECK( elapsedMs >= 1000 );
  CHECK( elapsedMs <= 5000 );

  // Fail-closed parity with the other wire branches: the stream position past
  // a half-frame is unusable, so the session is dead for good and never
  // handed a replay or a stale late answer.
  CHECK( session->permanentlyUnavailable() );
  REQUIRE_THROWS_AS( session->inferNamed( scalarInput( "x", 1.0f ), {} ), std::runtime_error );

  // The dead session must not poison the registry: a NEW legal session (fresh
  // identity — different script and weights bytes) serves a known answer.
  const ModelInfo healthy =
    makeWorkerModel( dir, dataDir() + "/py_worker_fake.py", "recover.bin", 30000 );
  const auto revived =
    ModelRuntimeRegistry::instance().acquire( healthy, RequestedDevice::cpu(), &error );
  INFO( "revive acquire error: " << error );
  REQUIRE( revived );
  const auto outputs = revived->inferNamed( scalarInput( "x", 5.0f ), {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs[0].second.dataFloat32()[0] == Catch::Approx( 5.0f ) );
}

TEST_CASE( "R35-T02 WP01: a legal slow segmented frame inside the deadline succeeds",
           "[models][python][r35t02]" )
{
  ( void )ensureApp();
  RegistryReset registryGuard;
  QTemporaryDir dir;
  // Four segments 150 ms apart (~450 ms total) inside a 3000 ms deadline: the
  // absolute deadline must not reject legal progressive frames.
  const ModelInfo model = makeWorkerModel(
    dir, dataDir() + "/py_worker_segmented.py", "segmented.bin", 3000 );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  INFO( "acquire error: " << error );
  REQUIRE( session );

  const auto outputs = session->inferNamed( scalarInput( "x", 6.0f ), {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs[0].second.dataFloat32()[0] == Catch::Approx( 6.0f ) );
  CHECK( session->health().forwardsCompleted == 1 );
  CHECK_FALSE( session->permanentlyUnavailable() );

  // A second segmented exchange on the same session re-enters the read loop
  // from a clean stream position (no stale half-frame carry-over).
  const auto again = session->inferNamed( scalarInput( "x", -2.0f ), {} );
  REQUIRE( again.size() == 1 );
  CHECK( again[0].second.dataFloat32()[0] == Catch::Approx( -2.0f ) );
}

TEST_CASE( "R35-T02 WP01: a fully silent worker fails at the deadline",
           "[models][python][r35t02]" )
{
  ( void )ensureApp();
  RegistryReset registryGuard;
  QTemporaryDir dir;
  const ModelInfo model =
    makeWorkerModel( dir, dataDir() + "/py_worker_silent.py", "silent.bin", 1200 );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  INFO( "acquire error: " << error );
  REQUIRE( session );

  const auto started = std::chrono::steady_clock::now();
  std::string message;
  try
  {
    session->inferNamed( scalarInput( "x", 1.0f ), {} );
    FAIL( "expected the silent worker to fail the forward" );
  }
  catch ( const std::exception &e )
  {
    message = e.what();
  }
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started )
                           .count();
  INFO( "forward failed after " << elapsedMs << " ms: " << message );

  CHECK( classifyInferenceError( message ) == InferenceFailureKind::Timeout );
  CHECK_THAT( message, Catch::Matchers::ContainsSubstring( "did not answer within 1200 ms" ) );
  CHECK( elapsedMs >= 1000 );
  CHECK( elapsedMs <= 5000 );
  CHECK( session->permanentlyUnavailable() );
}

TEST_CASE( "R35-T02 WP01: stderr chatter with silent stdout does not stall the deadline",
           "[models][python][r35t02]" )
{
  ( void )ensureApp();
  RegistryReset registryGuard;
  QTemporaryDir dir;
  const ModelInfo model = makeWorkerModel(
    dir, dataDir() + "/py_worker_stderr_chatter.py", "chatter.bin", 1200 );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  INFO( "acquire error: " << error );
  REQUIRE( session );

  const auto started = std::chrono::steady_clock::now();
  std::string message;
  try
  {
    session->inferNamed( scalarInput( "x", 1.0f ), {} );
    FAIL( "expected the chattering worker to fail the forward" );
  }
  catch ( const std::exception &e )
  {
    message = e.what();
  }
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started )
                           .count();
  INFO( "forward failed after " << elapsedMs << " ms: " << message );

  // The stderr drain runs on the same cadence; it must neither postpone the
  // stdout deadline nor mask the timeout classification. The drained tail is
  // attached to the failure DIAGNOSTIC (health().lastError), not the thrown
  // message — assert it there.
  CHECK( classifyInferenceError( message ) == InferenceFailureKind::Timeout );
  CHECK_THAT( message, Catch::Matchers::ContainsSubstring( "did not answer within 1200 ms" ) );
  CHECK_THAT( session->health().lastError,
              Catch::Matchers::ContainsSubstring( "stderr chatter stdout silent" ) );
  CHECK( elapsedMs >= 1000 );
  CHECK( elapsedMs <= 5000 );
  CHECK( session->permanentlyUnavailable() );
}
