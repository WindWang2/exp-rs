// tests/test_provider_python.cpp — Platform 7.0 Python worker provider over
// real out-of-process stdlib workers (tests/data/py_worker_fake.py and
// py_worker_crash.py): known-answer inference through the wire contract,
// artifact announcement, and ProviderCrash mapping on worker death.
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

#include <cmath>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;

/// QProcess needs a QCoreApplication; Catch2 owns main, so create it lazily.
QCoreApplication &ensureApp()
{
  static int argc = 1;
  static char name[] = "test_provider_python";
  static char *argv[] = { name, nullptr };
  static QCoreApplication app( argc, argv );
  return app;
}

ModelInfo makeWorkerModel( const QTemporaryDir &dir, const std::string &workerScript,
                           const std::string &weightsName = "weights.bin" )
{
  const QString weights = dir.filePath( QString::fromStdString( weightsName ) );
  QFile file( weights );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  // Unique bytes per test case: the session pool shares sessions by CONTENT
  // digest, so equal bytes would silently reuse another case's worker.
  // Bytes unique per (script, weights name): the pool shares sessions by
  // CONTENT digest, so the poisoned model must not share the healthy one's
  // session (which would announce the healthy artifact to the worker).
  file.write( QByteArray( "python-fake-weights:" ) + QString::fromStdString( workerScript ).toUtf8()
                + QByteArray( ":" ) + QString::fromStdString( weightsName ).toUtf8() );
  file.close();

  ModelInfo model;
  model.name = "m5-python";
  model.framework = "python";
  model.readiness = ModelReadiness::Ready;
  model.resolvedArtifactPath = weights.toStdString();
  model.runtime.provider.workerScript = workerScript;
  // The manifest path anchors relative worker scripts; point it at the
  // tests/data directory so the shipped workers resolve.
  model.sourceManifest =
    ( QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/manifest.json" ) )
      .toStdString();
  return model;
}

} // namespace

TEST_CASE( "python worker known answer through the registry seam", "[models][python]" )
{
  ( void )ensureApp();
  QTemporaryDir dir;
  const std::string script =
    ( QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/py_worker_fake.py" ) )
      .toStdString();
  const ModelInfo model = makeWorkerModel( dir, script );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  INFO( "acquire error: " << error );
  REQUIRE( session );

  CHECK( session->framework() == "python" );
  CHECK( session->backendName() == "python_worker" );
  const auto caps = session->capabilities();
  CHECK( caps.multiInput );
  CHECK( caps.namedBind );

  std::vector<float> a = { 5.0f, 9.0f };
  std::vector<float> b = { 3.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "before", TensorBlob::fromFloat32( { 1, 1, 1, 2 }, a.data(), 2 ) } );
  inputs.push_back( NamedTensor{ "after", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, b.data(), 1 ) } );
  const auto outputs = session->inferNamed( inputs, {} );
  REQUIRE( outputs.size() == 1 );
  // Last element of the first tensor is 9, plus 3 → 12.
  CHECK( outputs[0].second.dataFloat32()[0] == Catch::Approx( 12.0f ) );
  CHECK( session->health().forwardsCompleted == 1 );
}

TEST_CASE( "python worker errors surface the worker's own message", "[models][python]" )
{
  ( void )ensureApp();
  QTemporaryDir dir;
  const std::string script =
    ( QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/py_worker_fake.py" ) )
      .toStdString();
  const ModelInfo model = makeWorkerModel( dir, script, "poison.bin" );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  REQUIRE( session );

  std::vector<float> a = { 1.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } );
  try
  {
    session->inferNamed( inputs, {} );
    FAIL( "expected the poisoned artifact to fail" );
  }
  catch ( const std::exception &e )
  {
    CHECK_THAT( e.what(), Catch::Matchers::ContainsSubstring( "worker refuses poisoned weights" ) );
  }
}

TEST_CASE( "worker death maps to ProviderCrash", "[models][python]" )
{
  ( void )ensureApp();
  QTemporaryDir dir;
  const std::string script =
    ( QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/py_worker_crash.py" ) )
      .toStdString();
  const ModelInfo model = makeWorkerModel( dir, script );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  REQUIRE( session );

  std::vector<float> a = { 1.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } );
  // First inference succeeds; the worker then hard-exits.
  const auto outputs = session->inferNamed( inputs, {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs[0].second.dataFloat32()[0] == Catch::Approx( 42.0f ) );

  // Platform 8.0 WP-F: the SECOND inference hits the dead worker and
  // consumes the session's ONE bounded restart — the worker respawns,
  // re-hand-shakes and the forward replays (the crash worker answers 42
  // deterministically).
  {
    const auto replayed = session->inferNamed( inputs, {} );
    REQUIRE( replayed.size() == 1 );
    CHECK( replayed[0].second.dataFloat32()[0] == Catch::Approx( 42.0f ) );
  }

  // The THIRD inference sees a dead worker again, but the restart budget is
  // exhausted: typed ProviderCrash, never a respawn loop.
  REQUIRE_THROWS_AS( session->inferNamed( inputs, {} ), std::runtime_error );
  try
  {
    session->inferNamed( inputs, {} );
  }
  catch ( const std::exception &e )
  {
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::ProviderCrash );
    CHECK_THAT( e.what(),
                Catch::Matchers::ContainsSubstring( "restart budget is exhausted" ) );
  }

  // Track 13 crash recovery: the exhausted budget is the for-good death —
  // the session must report itself permanently unavailable so the registry
  // recycles the cached corpse instead of serving it forever.
  CHECK( session->permanentlyUnavailable() );
}

TEST_CASE( "missing worker script fails the load loudly", "[models][python]" )
{
  ( void )ensureApp();
  QTemporaryDir dir;
  const ModelInfo model = makeWorkerModel( dir, "/nonexistent/worker.py" );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  CHECK_FALSE( session );
  CHECK( error.find( "python worker script not found" ) != std::string::npos );
}

TEST_CASE( "worker handshake capabilities override the provider defaults",
           "[models][python][caps]" )
{
  ( void )ensureApp();
  QTemporaryDir dir;
  const std::string script =
    ( QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/py_worker_caps.py" ) )
      .toStdString();
  const ModelInfo model = makeWorkerModel( dir, script, "caps-weights.bin" );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  REQUIRE( session );

  // The worker declared: max_rank 5, no multi-input, float32-only inputs.
  const auto caps = session->capabilities();
  CHECK( caps.maxRank == 5 );
  CHECK_FALSE( caps.multiInput );
  REQUIRE( caps.inputDtypes.size() == 1 );
  CHECK( caps.inputDtypes.front() == "float32" );

  // Inference still works within the negotiated surface.
  std::vector<float> a = { 7.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } );
  const auto outputs = session->inferNamed( inputs, {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs[0].second.dataFloat32()[0] == Catch::Approx( 7.0f ) );
}

TEST_CASE( "shared python worker session survives concurrent probes of its negotiation state "
           "(#1056)",
           "[models][python][concurrency]" )
{
  ( void )ensureApp();
  // The registry hands ONE session to multiple TaskCenter threads. While the
  // main thread runs forwards — including the bounded-restart path that
  // REWRITES the negotiated handshake block — a foreign thread hammers
  // capabilities()/health()/providerDetails(). The crash worker declares a
  // NON-EMPTY capabilities block, so the restart genuinely rewrites a
  // populated JSON object mid-read (the pre-#1056 code raced that write
  // unsynchronized; now every read is a snapshot under m_stateMutex or an
  // atomic load).
  QTemporaryDir dir;
  const std::string script =
    ( QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/py_worker_crash.py" ) )
      .toStdString();
  const ModelInfo model = makeWorkerModel( dir, script, "probe-hammer-weights.bin" );
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  REQUIRE( session );

  std::vector<float> a = { 2.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } );

  std::atomic<unsigned> probes{ 0 };
  std::atomic<bool> stop{ false };
  std::thread prober( [ & ] {
    while ( !stop.load( std::memory_order_relaxed ) )
    {
      // No Catch2 assertions off the main thread: the assertion here is the
      // sanitizer's (and the absence of a torn-read crash).
      const auto caps = session->capabilities();
      const auto details = session->providerDetails();
      const auto health = session->health();
      ( void )caps;
      ( void )details;
      ( void )health;
      probes.fetch_add( 1, std::memory_order_relaxed );
    }
  } );

  // Forward #1 succeeds. Forward #2 crashes the worker mid-session and takes
  // the bounded-restart path (stop → fresh handshake → negotiated rewrite)
  // while the prober is mid-read. Forward #3 exhausts the restart budget and
  // must surface the typed ProviderCrash diagnostic.
  const auto first = session->inferNamed( inputs, {} );
  REQUIRE( first.size() == 1 );
  CHECK( first[0].second.dataFloat32()[0] == Catch::Approx( 42.0f ) );

  const auto replayed = session->inferNamed( inputs, {} );
  REQUIRE( replayed.size() == 1 );
  CHECK( replayed[0].second.dataFloat32()[0] == Catch::Approx( 42.0f ) );

  bool typedCrash = false;
  try
  {
    session->inferNamed( inputs, {} );
  }
  catch ( const std::exception &e )
  {
    typedCrash =
      classifyInferenceError( e.what() ) == InferenceFailureKind::ProviderCrash;
  }
  CHECK( typedCrash );

  stop.store( true, std::memory_order_relaxed );
  prober.join();
  CHECK( probes.load( std::memory_order_relaxed ) > 0 );
  CHECK( session->health().forwardsCompleted == 2 );

  // The crash worker DECLARES a non-empty capabilities block, so the
  // restart above genuinely rewrote a populated m_negotiated while the
  // prober was reading it. After the dust settles the republished block
  // must be the one the (re)handshake delivered — a torn or half-reset
  // negotiation state cannot produce this.
  CHECK( session->capabilities().maxRank == 5 );
  CHECK( session->providerDetails().executionProvider == "crashfake-ep" );
}

// Review P1-6: a model manifest is data — it must not choose an arbitrary
// program to run, and its worker script must live next to the manifest.
TEST_CASE( "manifest interpreter is restricted to Python launchers or configured paths",
           "[models][python][security]" )
{
  CHECK( modelInterpreterAllowed( "" ) );
  CHECK( modelInterpreterAllowed( "python3" ) );
  CHECK( modelInterpreterAllowed( "python" ) );
  CHECK( modelInterpreterAllowed( "python3.12" ) );
  CHECK( modelInterpreterAllowed( "py" ) );
  CHECK( modelInterpreterAllowed( "python.exe" ) );

  std::string reason;
  CHECK_FALSE( modelInterpreterAllowed( "cmd.exe", &reason ) );
  CHECK( reason.find( "not allowed" ) != std::string::npos );
  CHECK_FALSE( modelInterpreterAllowed( "sh" ) );
  CHECK_FALSE( modelInterpreterAllowed( "bash" ) );
  CHECK_FALSE( modelInterpreterAllowed( "python3; rm -rf /" ) );

  // A path that merely LOOKS like python is rejected unless configured.
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString fakePython = dir.filePath( QStringLiteral( "python3" ) );
  {
    QFile f( fakePython );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "#!/bin/sh\necho pwned\n" );
  }
  const QByteArray savedList = qgetenv( "SICNU_MODEL_INTERPRETERS" );
  const QByteArray savedExec = qgetenv( "SICNU_PYTHON_EXECUTABLE" );
  qunsetenv( "SICNU_MODEL_INTERPRETERS" );
  qunsetenv( "SICNU_PYTHON_EXECUTABLE" );
  CHECK_FALSE( modelInterpreterAllowed( fakePython.toStdString() ) );
  qputenv( "SICNU_MODEL_INTERPRETERS", fakePython.toUtf8() );
  CHECK( modelInterpreterAllowed( fakePython.toStdString() ) );
  qunsetenv( "SICNU_MODEL_INTERPRETERS" );
  qputenv( "SICNU_PYTHON_EXECUTABLE", fakePython.toUtf8() );
  CHECK( modelInterpreterAllowed( fakePython.toStdString() ) );
  if ( savedList.isNull() ) qunsetenv( "SICNU_MODEL_INTERPRETERS" ); else qputenv( "SICNU_MODEL_INTERPRETERS", savedList );
  if ( savedExec.isNull() ) qunsetenv( "SICNU_PYTHON_EXECUTABLE" ); else qputenv( "SICNU_PYTHON_EXECUTABLE", savedExec );
}

TEST_CASE( "manifest with a foreign interpreter is refused at acquire", "[models][python][security]" )
{
  ( void )ensureApp();
  QTemporaryDir dir;
  const std::string script =
    ( QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/py_worker_fake.py" ) )
      .toStdString();
  ModelInfo model = makeWorkerModel( dir, script, "foreign-interp.bin" );
  model.runtime.provider.interpreter = "/bin/sh";
  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  CHECK_FALSE( session );
  CHECK( error.find( "not allowed" ) != std::string::npos );
}

TEST_CASE( "manifest worker_script must stay inside the manifest directory", "[models][python][security]" )
{
  QTemporaryDir root;
  REQUIRE( root.isValid() );
  REQUIRE( QDir( root.path() ).mkpath( QStringLiteral( "model/workers" ) ) );
  REQUIRE( QDir( root.path() ).mkpath( QStringLiteral( "elsewhere" ) ) );
  for ( const QString &rel : { QStringLiteral( "model/workers/w.py" ), QStringLiteral( "elsewhere/evil.py" ) } )
  {
    QFile f( root.filePath( rel ) );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "print('x')\n" );
  }
  const std::string manifest = root.filePath( QStringLiteral( "model/model.json" ) ).toStdString();

  std::string resolved;
  std::string reason;
  REQUIRE( resolveModelWorkerScript( "workers/w.py", manifest, &resolved, &reason ) );
  CHECK( QFileInfo( QString::fromStdString( resolved ) ).fileName() == QStringLiteral( "w.py" ) );

  CHECK_FALSE( resolveModelWorkerScript( "../elsewhere/evil.py", manifest, &resolved, &reason ) );
  CHECK( reason.find( "outside the manifest directory" ) != std::string::npos );
  CHECK_FALSE( resolveModelWorkerScript( root.filePath( QStringLiteral( "elsewhere/evil.py" ) ).toStdString(),
                                         manifest, &resolved, &reason ) );
  CHECK_FALSE( resolveModelWorkerScript( "workers/missing.py", manifest, &resolved, &reason ) );
  CHECK( reason.find( "not found" ) != std::string::npos );

  // Symlink inside the model dir pointing outside is still outside.
  if ( QFile::link( root.filePath( QStringLiteral( "elsewhere/evil.py" ) ),
                    root.filePath( QStringLiteral( "model/workers/link.py" ) ) ) )
    CHECK_FALSE( resolveModelWorkerScript( "workers/link.py", manifest, &resolved, &reason ) );

  // Programmatic models (no manifest on disk) keep their explicit script.
  REQUIRE( resolveModelWorkerScript( "/abs/worker.py", "", &resolved, &reason ) );
  CHECK( resolved == "/abs/worker.py" );
}

TEST_CASE( "default model search never uses the working directory", "[models][catalog][security]" )
{
  ( void )ensureApp();
  QTemporaryDir untrusted;
  REQUIRE( untrusted.isValid() );
  REQUIRE( QDir( untrusted.path() ).mkpath( QStringLiteral( "models/evil" ) ) );

  const QByteArray savedModels = qgetenv( "SICNU_MODELS_DIR" );
  qunsetenv( "SICNU_MODELS_DIR" );
  const QString savedCwd = QDir::currentPath();
  REQUIRE( QDir::setCurrent( untrusted.path() ) );

  const QString chosen = QString::fromStdString( sicnu::operators::ModelCatalog::defaultModelsDirectory() );
  const QString cwdModels = QDir( untrusted.path() ).filePath( QStringLiteral( "models" ) );

  QDir::setCurrent( savedCwd );
  if ( !savedModels.isNull() )
    qputenv( "SICNU_MODELS_DIR", savedModels );

  INFO( "chosen models dir: " << chosen.toStdString() );
  CHECK_FALSE( chosen.isEmpty() );
  CHECK( QFileInfo( chosen ).absoluteFilePath() != QFileInfo( cwdModels ).absoluteFilePath() );
  CHECK_FALSE( QFileInfo( chosen ).canonicalFilePath().startsWith( QFileInfo( untrusted.path() ).canonicalFilePath() ) );
}
