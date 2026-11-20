// tests/test_r35_t02_provider_manifest_typed.cpp — R35-T02 WP05: the provider
// knobs runtime.provider.timeout_ms / max_body_mb are TYPED manifest fields.
//
// Pre-fix model_catalog.cpp read both with QJsonValue::toInt(default): a
// string ("30000"), a bool, an object, a non-integral double (1.5) or an
// out-of-int-range double (1e12) all silently became the DEFAULT knob — the
// manifest author's declaration vanished without a diagnostic. This matrix
// pins the typed contract at the catalog entry (rejection names the field
// path, before any process or network activity) and the domain check at the
// reachable factory entry for PROGRAMMATIC ModelInfo (negatives refuse at
// acquire; 0 keeps its documented "provider default" meaning everywhere).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <string>
#include <vector>

namespace
{

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using namespace sicnu::operators::runtime;

QCoreApplication &ensureApp()
{
  static int argc = 1;
  static char name[] = "test_r35_t02_provider_manifest_typed";
  static char *argv[] = { name, nullptr };
  static QCoreApplication app( argc, argv );
  return app;
}

struct RegistryReset
{
  RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
  ~RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
};

/// A historical python-framework manifest (the worker_script the framework
/// check demands) plus one extra provider key with the given raw JSON token.
std::string manifestWithKnob( const std::string &name, const char *knob, const char *rawToken )
{
  return std::string( R"({
      "name": ")" ) + name
         + R"(", "task": "segmentation", "framework": "python",
      "runtime": { "provider": { "worker_script": "workers/w.py", ")"
         + knob + R"(": )" + rawToken + R"( } }
  })";
}

/// True when validateManifestJson reports an issue containing @p needle.
bool refusalMentions( const std::string &json, const std::string &needle, std::string *allIssues )
{
  const auto issues = ModelCatalog::instance().validateManifestJson( json );
  std::string all;
  bool found = false;
  for ( const auto &issue : issues )
  {
    found = found || issue.find( needle ) != std::string::npos;
    all += "\n  - " + issue;
  }
  if ( allIssues )
    *allIssues = all;
  return found;
}

} // namespace

TEST_CASE( "R35-T02 WP05: provider knobs are typed at the catalog entry",
           "[models][catalog][r35t02]" )
{
  ( void )ensureApp();

  SECTION( "wrong-typed timeout_ms rejects naming the field path" )
  {
    const char *tokens[] = { "\"30000\"", "true", "1.5", "-5", "1000000000000", "[30000]",
                             "{\"ms\":1}" };
    for ( const char *raw : tokens )
    {
      std::string all;
      const bool refused = refusalMentions( manifestWithKnob( "r35t02-bad-timeout", "timeout_ms", raw ),
                                            "runtime.provider.timeout_ms", &all );
      INFO( "timeout_ms raw token: " << raw << " → issues:" << all );
      CHECK( refused );
    }
  }

  SECTION( "wrong-typed max_body_mb rejects naming the field path" )
  {
    const char *tokens[] = { "\"256\"", "false", "2.5", "-1", "1000000000000" };
    for ( const char *raw : tokens )
    {
      std::string all;
      const bool refused = refusalMentions( manifestWithKnob( "r35t02-bad-body", "max_body_mb", raw ),
                                            "runtime.provider.max_body_mb", &all );
      INFO( "max_body_mb raw token: " << raw << " → issues:" << all );
      CHECK( refused );
    }
  }

  SECTION( "historical manifests keep the documented defaults" )
  {
    // No timeout/max knob at all (worker_script only) → defaults, and the
    // explicit legal values round-trip through registration + find().
    auto &catalog = ModelCatalog::instance();
    std::string error;
    const std::string defaultsJson = R"({
      "name": "r35t02-hist-defaults", "task": "segmentation", "framework": "python",
      "runtime": { "provider": { "worker_script": "workers/w.py" } }
    })";
    REQUIRE( catalog.registerManifestJson( defaultsJson, "test://r35t02-hist-defaults", &error ) );
    const auto defaultsEntry = catalog.find( "r35t02-hist-defaults" );
    REQUIRE( defaultsEntry.has_value() );
    CHECK( defaultsEntry->runtime.provider.timeoutMs == 30000 );
    CHECK( defaultsEntry->runtime.provider.maxBodyMb == 256 );

    // null = not declared → default; 0 = documented "provider default"
    // sentinel → accepted, value carried through.
    const std::string nullJson = manifestWithKnob( "r35t02-hist-null", "timeout_ms", "null" );
    REQUIRE( catalog.registerManifestJson( nullJson, "test://r35t02-hist-null", &error ) );
    const auto nullEntry = catalog.find( "r35t02-hist-null" );
    REQUIRE( nullEntry.has_value() );
    CHECK( nullEntry->runtime.provider.timeoutMs == 30000 );

    const std::string zeroBodyJson =
      manifestWithKnob( "r35t02-hist-zero", "max_body_mb", "0" );
    REQUIRE( catalog.registerManifestJson( zeroBodyJson, "test://r35t02-hist-zero", &error ) );
    const auto zeroEntry = catalog.find( "r35t02-hist-zero" );
    REQUIRE( zeroEntry.has_value() );
    CHECK( zeroEntry->runtime.provider.maxBodyMb == 0 );

    const std::string legalJson =
      manifestWithKnob( "r35t02-hist-legal", "timeout_ms", "2500" );
    REQUIRE( catalog.registerManifestJson( legalJson, "test://r35t02-hist-legal", &error ) );
    const auto legalEntry = catalog.find( "r35t02-hist-legal" );
    REQUIRE( legalEntry.has_value() );
    CHECK( legalEntry->runtime.provider.timeoutMs == 2500 );
    CHECK( legalEntry->runtime.provider.maxBodyMb == 256 ); // untouched default
  }
}

TEST_CASE( "R35-T02 WP05: programmatic ModelInfo knobs are domain-checked at the "
           "provider entry",
           "[models][python][r35t02]" )
{
  ( void )ensureApp();
  RegistryReset registryGuard;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const std::string script =
    ( QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/py_worker_fake.py" ) )
      .toStdString();
  const auto makeModel = [ &dir, &script ]( const std::string &weights, int timeoutMs,
                                            long maxBodyMb ) {
    const QString weightsPath = dir.filePath( QString::fromStdString( weights ) );
    QFile file( weightsPath );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QByteArray( "r35t02-typed-weights:" ) + QString::fromStdString( weights ).toUtf8() );
    file.close();
    ModelInfo model;
    model.name = "r35t02-typed";
    model.framework = "python";
    model.readiness = ModelReadiness::Ready;
    model.resolvedArtifactPath = weightsPath.toStdString();
    model.runtime.provider.workerScript = script;
    model.runtime.provider.timeoutMs = timeoutMs;
    model.runtime.provider.maxBodyMb = maxBodyMb;
    return model;
  };

  // A negative knob refuses at acquire — BEFORE any worker process starts —
  // with the field path, instead of silently running with the default.
  std::string error;
  CHECK_FALSE( ModelRuntimeRegistry::instance().acquire(
                 makeModel( "neg-timeout.bin", -1000, 256 ), RequestedDevice::cpu(), &error ) );
  CHECK_THAT( error, Catch::Matchers::ContainsSubstring( "runtime.provider.timeout_ms" ) );
  CHECK_THAT( error, Catch::Matchers::ContainsSubstring( "negative" ) );

  error.clear();
  CHECK_FALSE( ModelRuntimeRegistry::instance().acquire(
                 makeModel( "neg-body.bin", 30000, -8 ), RequestedDevice::cpu(), &error ) );
  CHECK_THAT( error, Catch::Matchers::ContainsSubstring( "runtime.provider.max_body_mb" ) );
  CHECK_THAT( error, Catch::Matchers::ContainsSubstring( "negative" ) );

  // 0 keeps its documented "provider default" meaning: the session loads and
  // serves the known answer with the provider's own default timeout.
  const auto session = ModelRuntimeRegistry::instance().acquire(
    makeModel( "zero.bin", 0, 0 ), RequestedDevice::cpu(), &error );
  INFO( "acquire error: " << error );
  REQUIRE( session );
  float value = 9.0f;
  const auto outputs = session->inferNamed(
    { NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, &value, 1 ) } }, {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs[0].second.dataFloat32()[0] == Catch::Approx( 9.0f ) );
}
