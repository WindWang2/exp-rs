// tests/test_provider_fallback.cpp — provider strategy (ordered fallback
// chain over the SAME artifact): registry-level acquireWithFallback verdicts
// and the service-level payload reporting. A fallback that fires is never
// silent: the selection report names every attempt, and the winning
// framework flows into the provenance sidecar's model.framework.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/provenance_verify.h"
#include "synthetic_raster_builder.h"

#include <gdal_priv.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>
#include <string>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelExecutionResult;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::ProviderSelectionReport;
using sicnu::operators::runtime::RequestedDevice;
using sicnu::operators::runtime::verifyProductAgainstModel;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

/// Scale provider tagged with its framework id so tests can tell which
/// candidate won the acquisition.
class TaggedRuntime final : public IModelRuntime
{
  public:
    TaggedRuntime( std::string artifact, std::string framework )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ) {}

    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "tagged:" + m_framework; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }
    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }

  private:
    std::string m_artifact;
    std::string m_framework;
};

struct RegistryReset
{
    RegistryReset()
    {
      ModelRuntimeRegistry::instance().releaseAll();
      ModelRuntimeRegistry::instance().resetLoadCount();
      ModelRuntimeRegistry::instance().setMaxCachedSessions( 8 );
      ModelRuntimeRegistry::instance().setIdleEvictionMs( 0 );
    }
    ~RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
};

/// Registers a provider whose factory fails with @p message ("" = succeeds).
struct ScriptedProvider
{
    ScriptedProvider( const std::string &framework, const std::string &failureMessage )
    {
      ModelRuntimeRegistry::instance().registerProvider(
        framework,
        [ framework, failureMessage ]( const ModelInfo &model,
                                       const ModelHardwareCapabilities &,
                                       std::string *error ) -> ModelRuntimePtr {
          if ( !failureMessage.empty() )
          {
            if ( error )
              *error = failureMessage;
            return nullptr;
          }
          return std::make_shared<TaggedRuntime>( model.resolvedArtifactPath, framework );
        } );
    }
};

ModelInfo fallbackModel( const std::string &primary, const std::vector<std::string> &chain )
{
  ModelInfo info;
  info.name = "fallback-model";
  info.task = "segmentation";
  info.framework = primary;
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = identityModelPath().toStdString();
  info.runtime.frameworkFallback = chain;
  return info;
}

} // namespace

TEST_CASE( "acquireWithFallback walks the chain in order over the same artifact",
           "[models][fallback]" )
{
  RegistryReset reset;
  auto &registry = ModelRuntimeRegistry::instance();

  SECTION( "primary failure promotes the first working fallback" )
  {
    const ScriptedProvider broken( "fb-primary", "primary provider is down" );
    const ScriptedProvider working( "fb-second", "" );
    const ModelInfo model = fallbackModel( "fb-primary", { "fb-second" } );

    ProviderSelectionReport report;
    std::string error;
    const auto session = registry.acquireWithFallback( model, nullptr, &error, &report );
    REQUIRE( session != nullptr );
    CHECK( session->framework() == "fb-second" );
    CHECK( report.resolvedFramework == "fb-second" );
    REQUIRE( report.attempts.size() == 2 );
    CHECK_FALSE( report.attempts[0].loaded );
    CHECK( report.attempts[0].detail == "primary provider is down" );
    CHECK( report.attempts[1].loaded );
    CHECK( error.empty() );
  }

  SECTION( "an unregistered candidate is a recorded skip, not a fabrication" )
  {
    const ScriptedProvider working( "fb-second", "" );
    const ModelInfo model = fallbackModel( "fb-primary", { "fb-ghost", "fb-second" } );

    ProviderSelectionReport report;
    const auto session = registry.acquireWithFallback( model, nullptr, nullptr, &report );
    REQUIRE( session != nullptr );
    REQUIRE( report.attempts.size() == 3 );
    // attempts[0] is the PRIMARY (registered but scripted broken here);
    // attempts[1] is the unregistered ghost — recorded, never fabricated.
    CHECK( report.attempts[0].providerRegistered );
    CHECK_FALSE( report.attempts[0].loaded );
    CHECK_FALSE( report.attempts[1].providerRegistered );
    CHECK( report.attempts[1].detail.find( "no runtime provider" ) != std::string::npos );
    CHECK( report.attempts[2].providerRegistered );
    CHECK( report.attempts[2].loaded );
    CHECK( report.resolvedFramework == "fb-second" );
  }

  SECTION( "a fully failing chain is one typed error with every reason" )
  {
    const ScriptedProvider broken1( "fb-primary", "primary down" );
    const ScriptedProvider broken2( "fb-second", "second down" );
    const ModelInfo model = fallbackModel( "fb-primary", { "fb-second" } );

    ProviderSelectionReport report;
    std::string error;
    const auto session = registry.acquireWithFallback( model, nullptr, &error, &report );
    CHECK( session == nullptr );
    CHECK( report.resolvedFramework.empty() );
    REQUIRE( report.attempts.size() == 2 );
    CHECK_FALSE( report.attempts[0].loaded );
    CHECK_FALSE( report.attempts[1].loaded );
    INFO( "aggregated error was: " << error );
    CHECK( error.find( "primary down" ) != std::string::npos );
    CHECK( error.find( "second down" ) != std::string::npos );
    CHECK( error.find( "same artifact" ) != std::string::npos );
  }

  SECTION( "a working primary never touches the chain" )
  {
    const ScriptedProvider working( "fb-primary", "" );
    const ScriptedProvider unused( "fb-second", "" );
    const ModelInfo model = fallbackModel( "fb-primary", { "fb-second" } );

    ProviderSelectionReport report;
    const auto session = registry.acquireWithFallback( model, nullptr, nullptr, &report );
    REQUIRE( session != nullptr );
    CHECK( report.attempts.size() == 1 );
    CHECK( report.resolvedFramework == "fb-primary" );
  }

  SECTION( "an explicit device request applies to every candidate" )
  {
    const ScriptedProvider working( "fb-second", "" );
    const ModelInfo model = fallbackModel( "fb-primary", { "fb-second" } );
    ProviderSelectionReport report;
    const RequestedDevice cpu = RequestedDevice::cpu();
    const auto session = registry.acquireWithFallback( model, &cpu, nullptr, &report );
    REQUIRE( session != nullptr );
    CHECK( session->deviceName() == "cpu" );
  }

  SECTION( "different winning frameworks never share a session" )
  {
    // Same artifact bytes, two executors: the session cache key carries the
    // framework, so the fallback winner and the primary winner must be two
    // distinct sessions even for identical weights.
    const ScriptedProvider brokenNow( "fb-primary", "primary down for the chain run" );
    const ScriptedProvider workingSecond( "fb-second", "" );
    const ModelInfo viaChain = fallbackModel( "fb-primary", { "fb-second" } );
    const auto viaFallback = registry.acquireWithFallback( viaChain, nullptr, nullptr, nullptr );
    REQUIRE( viaFallback );
    CHECK( viaFallback->framework() == "fb-second" );

    const ScriptedProvider workingPrimary( "fb-primary", "" );
    ModelInfo directModel = viaChain;
    directModel.runtime.frameworkFallback.clear();
    const auto direct = registry.acquire( directModel, nullptr );
    REQUIRE( direct );
    CHECK( direct->framework() == "fb-primary" );
    CHECK( direct != viaFallback );
  }
}

TEST_CASE( "fallback acquisitions are reported in the payload and provenance",
           "[models][fallback][service]" )
{
  RegistryReset reset;
  const ScriptedProvider broken( "svc-primary", "primary provider unavailable" );
  const ScriptedProvider working( "svc-second", "" );

  QTemporaryDir dir;
  const std::string artifact = identityModelPath().toStdString();
  const std::string manifestJson = R"json({
    "name": "svc-fb-model",
    "task": "segmentation",
    "framework": "svc-primary",
    "artifact": { "path": ")json" + artifact + R"json(" },
    "runtime": { "framework_fallback": ["svc-second"] },
    "output": { "classes": ["a", "b"] }
  })json";
  std::string registerError;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    manifestJson, dir.filePath( QStringLiteral( "svc/model.json" ) ).toStdString(),
    &registerError ) );

  const QString input = dir.filePath( QStringLiteral( "input.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 2, GDT_Float32 )
    .withConstantValue( 1, 1.0f )
    .withConstantValue( 2, 2.0f )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "out.tif" ) );

  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "svc-fb-model";
  RSOperatorContext context;
  ModelExecutionResult result;
  REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );

  // The payload names the winner and carries the attempt trail.
  CHECK( result.payload["provider_selection"]["resolved_framework"].asString() == "svc-second" );
  CHECK( result.payload["provider_selection"]["attempts"].size() == 2 );
  CHECK( result.payload["provider_selection"]["attempts"][0]["loaded"].asBool() == false );
  CHECK( result.payload["provider_selection"]["attempts"][1]["loaded"].asBool() == true );
  CHECK( result.backend == "tagged:svc-second" );

  // The provenance sidecar names the framework that ACTUALLY executed.
  const auto verdict = verifyProductAgainstModel( output.toStdString(), "svc-fb-model" );
  CHECK( verdict.state == sicnu::operators::runtime::ProvenanceVerdict::State::Ok );
  CHECK( verdict.provenance["model"]["framework"].asString() == "svc-second" );
  CHECK( verdict.provenance["execution"]["backend"].asString() == "tagged:svc-second" );
}

TEST_CASE( "runtime readiness is chain-aware: any ready candidate is ready",
           "[models][fallback][readiness]" )
{
  RegistryReset reset;
  auto &registry = ModelRuntimeRegistry::instance();

  ModelHardwareCapabilities hw; // no GPU claims — plain CPU feasibility
  const ModelInfo model = fallbackModel( "ro-ghost-primary", { "ro-ghost-fallback" } );

  SECTION( "nothing registered — verdict stays the primary's with the trail" )
  {
    std::string reason;
    CHECK( sicnu::operators::runtime::evaluateRuntimeReadiness( model, hw, &reason )
           != ModelReadiness::Ready );
    CHECK( reason.find( "ro-ghost-primary" ) != std::string::npos );
  }

  SECTION( "a registered fallback makes the model runtime-ready" )
  {
    const ScriptedProvider working( "ro-ghost-fallback", "" );
    std::string reason;
    CHECK( sicnu::operators::runtime::evaluateRuntimeReadiness( model, hw, &reason )
           == ModelReadiness::Ready );
  }
}
