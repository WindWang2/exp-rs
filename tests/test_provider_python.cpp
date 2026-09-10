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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <string>
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

  // Second inference sees a dead worker: typed ProviderCrash.
  REQUIRE_THROWS_AS( session->inferNamed( inputs, {} ), std::runtime_error );
  try
  {
    session->inferNamed( inputs, {} );
  }
  catch ( const std::exception &e )
  {
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::ProviderCrash );
  }
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
