// tests/test_model_ensemble.cpp — manifest-defined model ensembles: contract
// parsing/validation, member resolution, weighted_mean / weighted_vote
// combination semantics against hand-computed answers, uncertainty bands,
// provenance sidecar membership, atomic failure (no partial output) and the
// surface exclusions (detection/scene-classify/multi-feed ensembles refuse).
// Uses counting fake providers (scale semantics) over the committed identity
// ONNX fixture and tiny synthetic rasters (<= 64 px).
//
// Manifest documents are built through jsoncpp (not string concatenation) so
// the declared contracts stay readable and the payloads cannot drift from
// the code that intends them.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <json/json.h>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_ensemble.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/provenance_verify.h"
#include "synthetic_raster_builder.h"

#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

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
using sicnu::operators::runtime::NamedRasterFeed;
using sicnu::operators::runtime::RasterOutputMode;
using sicnu::operators::runtime::verifyProductAgainstModel;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

/// Scale provider: NCHW in → NCHW out × scale (per-framework id), so member
/// outputs are exact, hand-checkable multiples of the constant input.
class ScaleRuntime final : public IModelRuntime
{
  public:
    ScaleRuntime( std::string artifact, std::string framework, double scale )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_scale( scale ) {}

    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "scale_backend"; }
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

/// Registers a scale provider factory for one framework id.
struct ScaleProviderGuard
{
    ScaleProviderGuard( const std::string &framework, double scale )
    {
      ModelRuntimeRegistry::instance().registerProvider(
        framework,
        [ framework, scale ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                              std::string *error ) -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.empty() )
          {
            if ( error )
              *error = "no resolved artifact";
            return nullptr;
          }
          return std::make_shared<ScaleRuntime>( model.resolvedArtifactPath, framework, scale );
        } );
    }
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

/// Single-member manifest document (artifact = the committed fixture).
Json::Value memberManifest( const std::string &name, const std::string &framework )
{
  Json::Value json( Json::objectValue );
  json["name"] = name;
  json["task"] = "segmentation";
  json["framework"] = framework;
  json["artifact"]["path"] = identityModelPath().toStdString();
  return json;
}

/// Registers one member through the programmatic registry. Fails the test
/// with the rejection reason when the catalog refuses it.
void registerMember( const std::string &name, const std::string &framework,
                     const QTemporaryDir &dir )
{
  const Json::Value json = memberManifest( name, framework );
  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ),
    dir.filePath( QString::fromStdString( name ) + QStringLiteral( "/model.json" ) ).toStdString(),
    &error );
  if ( !ok )
    FAIL( "member '" + name + "' rejected: " + error );
}

/// Registers the standard two-member ensemble (member scale semantics come
/// from the caller's provider guards).
void registerEnsemble( const QTemporaryDir &dir, const std::string &ensembleName,
                       double weightA = 1.0, double weightB = 1.0,
                       const std::string &combination = std::string(),
                       const std::string &uncertainty = std::string() )
{
  Json::Value json( Json::objectValue );
  json["name"] = ensembleName;
  json["task"] = "segmentation";
  json["framework"] = "onnx";
  Json::Value &ensemble = json["ensemble"] = Json::Value( Json::objectValue );
  Json::Value members( Json::arrayValue );
  Json::Value a( Json::objectValue );
  a["model"] = "member-a";
  a["weight"] = weightA;
  members.append( a );
  Json::Value b( Json::objectValue );
  b["model"] = "member-b";
  b["weight"] = weightB;
  members.append( b );
  ensemble["members"] = members;
  if ( !combination.empty() )
    ensemble["combination"] = combination;
  if ( !uncertainty.empty() )
    ensemble["uncertainty"] = uncertainty;

  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ),
    dir.filePath( QStringLiteral( "ens/model.json" ) ).toStdString(), &error );
  if ( !ok )
    FAIL( "ensemble '" + ensembleName + "' rejected: " + error );
}

/// Writes a constant float raster with @p bands channels of value @p value.
QString writeConstantRaster( const QTemporaryDir &dir, const QString &name, int size,
                             int bands, float value )
{
  const QString path = dir.filePath( name );
  sicnu::testing::RsSyntheticRasterBuilder builder( size, size, bands, GDT_Float32 );
  for ( int b = 1; b <= bands; ++b )
    builder.withConstantValue( b, value );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( path );
  return path;
}

int rasterBandCount( const QString &path )
{
  GDALDataset *ds = GDALDataset::Open( path.toUtf8().constData(), GA_ReadOnly );
  if ( !ds )
    return -1;
  const int bands = ds->GetRasterCount();
  GDALClose( ds );
  return bands;
}

/// Reads one value from a raster as float32.
float readPixel( const QString &path, int band, int x, int y, bool *ok )
{
  GDALDataset *ds = GDALDataset::Open( path.toUtf8().constData(), GA_ReadOnly );
  if ( !ds )
  {
    if ( ok )
      *ok = false;
    return 0.0f;
  }
  float value = 0.0f;
  const bool read = ds->GetRasterBand( band )->RasterIO( GF_Read, x, y, 1, 1, &value, 1, 1,
                                                         GDT_Float32, 0, 0 ) == CE_None;
  GDALClose( ds );
  if ( ok )
    *ok = read;
  return value;
}


/// Forward-counting identity provider (hardening 15/20): counts infer() calls
/// into a shared atomic (members may run concurrently) and returns the blob
/// unchanged, so the run would succeed if it were allowed to reach members.
class CountingRuntime final : public IModelRuntime
{
  public:
    CountingRuntime( std::string artifact, std::string framework,
                     std::shared_ptr<std::atomic<int>> forwards )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_forwards( std::move( forwards ) )
    {
    }
    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "counting_backend"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      m_forwards->fetch_add( 1 );
      return blob.clone();
    }

  private:
    std::string m_artifact;
    std::string m_framework;
    std::shared_ptr<std::atomic<int>> m_forwards;
};

struct CountingProviderGuard
{
    std::shared_ptr<std::atomic<int>> forwards;
    explicit CountingProviderGuard( const std::string &framework )
        : forwards( std::make_shared<std::atomic<int>>( 0 ) )
    {
      ModelRuntimeRegistry::instance().registerProvider(
        framework,
        [ framework, forwards = forwards ]( const ModelInfo &model,
                                            const ModelHardwareCapabilities &,
                                            std::string *error ) -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.empty() )
          {
            if ( error )
              *error = "no resolved artifact";
            return nullptr;
          }
          return std::make_shared<CountingRuntime>( model.resolvedArtifactPath, framework,
                                                    forwards );
        } );
    }
};

ModelExecutionRequest rasterRequest( const std::string &input, const std::string &output,
                                     const std::string &modelRef )
{
  ModelExecutionRequest request;
  request.inputPath = input;
  request.outputPath = output;
  request.modelReference = modelRef;
  return request;
}

} // namespace

TEST_CASE( "ensemble manifests parse with the contract vocabulary",
           "[models][ensemble][manifest]" )
{
  RegistryReset reset;

  auto issuesFor = []( const Json::Value &json ) {
    return ModelCatalog::instance().validateManifestJson(
      Json::writeString( Json::StreamWriterBuilder(), json ) );
  };

  SECTION( "member shorthand, combination and uncertainty tokens are legal" )
  {
    Json::Value json( Json::objectValue );
    json["name"] = "ens-ok";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    json["ensemble"]["members"] = Json::Value( Json::arrayValue );
    json["ensemble"]["members"].append( "m1" );
    json["ensemble"]["members"].append( "m2" );
    json["ensemble"]["combination"] = "weighted_vote";
    json["ensemble"]["uncertainty"] = "agreement";
    CHECK( issuesFor( json ).empty() );
  }

  SECTION( "fewer than two members is refused" )
  {
    Json::Value json( Json::objectValue );
    json["name"] = "ens-one";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    json["ensemble"]["members"].append( "m1" );
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "at least 2 members" ) != std::string::npos );
  }

  SECTION( "duplicate member references are refused" )
  {
    Json::Value json( Json::objectValue );
    json["name"] = "ens-dup";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    json["ensemble"]["members"].append( "m1" );
    json["ensemble"]["members"].append( "m1" );
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "twice" ) != std::string::npos );
  }

  SECTION( "seventeen members exceed the bounded member axis" )
  {
    Json::Value json( Json::objectValue );
    json["name"] = "ens-big";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    for ( int i = 0; i < 17; ++i )
      json["ensemble"]["members"].append( "m" + std::to_string( i ) );
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "maximum is 16" ) != std::string::npos );
  }

  SECTION( "contradicting uncertainty/combination pairs are refused" )
  {
    Json::Value json( Json::objectValue );
    json["name"] = "ens-unc";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    json["ensemble"]["members"].append( "m1" );
    json["ensemble"]["members"].append( "m2" );
    json["ensemble"]["combination"] = "weighted_vote";
    json["ensemble"]["uncertainty"] = "variance";
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "contradicts" ) != std::string::npos );
  }

  SECTION( "an ensemble manifest with its own artifact is refused" )
  {
    Json::Value json( Json::objectValue );
    json["name"] = "ens-artifact";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    json["artifact"]["path"] = "weights.onnx";
    json["ensemble"]["members"].append( "m1" );
    json["ensemble"]["members"].append( "m2" );
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "no weights of its own" ) != std::string::npos );
  }

  SECTION( "infinite weights are refused" )
  {
    Json::Value json( Json::objectValue );
    json["name"] = "ens-weight";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    Json::Value a( Json::objectValue );
    a["model"] = "m1";
    a["weight"] = 1e300;
    json["ensemble"]["members"].append( a );
    Json::Value b( Json::objectValue );
    b["model"] = "m2";
    b["weight"] = 1.0;
    json["ensemble"]["members"].append( b );
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "weight" ) != std::string::npos );
  }

  SECTION( "unknown ensemble keys are refused (closed vocabulary)" )
  {
    Json::Value json( Json::objectValue );
    json["name"] = "ens-unknown";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    json["ensemble"]["members"].append( "m1" );
    json["ensemble"]["members"].append( "m2" );
    json["ensemble"]["stratgey"] = "typo";
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "ensemble." ) != std::string::npos );
  }
}

TEST_CASE( "ensemble member references are validated against the catalog",
           "[models][ensemble][members]" )
{
  RegistryReset reset;
  QTemporaryDir dir;

  auto registerNamed = [ & ]( const std::string &name, const Json::Value &ensemble ) {
    Json::Value json( Json::objectValue );
    json["name"] = name;
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    json["ensemble"] = ensemble;
    std::string error;
    return ModelCatalog::instance().registerManifestJson(
      Json::writeString( Json::StreamWriterBuilder(), json ),
      dir.filePath( QString::fromStdString( name ) + QStringLiteral( "/model.json" ) ).toStdString(),
      &error );
  };

  SECTION( "a missing member makes the ensemble invalid, not silent" )
  {
    Json::Value ensemble( Json::objectValue );
    ensemble["members"].append( "ghost-a" );
    ensemble["members"].append( "ghost-b" );
    REQUIRE( registerNamed( "ens-missing", ensemble ) );
    const auto model = ModelCatalog::instance().find( "ens-missing" );
    REQUIRE( model.has_value() );
    CHECK( model->readiness == ModelReadiness::InvalidManifest );
    CHECK( model->readinessReason.find( "resolves to no catalog model" ) != std::string::npos );
  }

  SECTION( "a self-referencing ensemble is refused" )
  {
    Json::Value ensemble( Json::objectValue );
    ensemble["members"].append( "ens-self" );
    ensemble["members"].append( "member-b" );
    REQUIRE( registerNamed( "ens-self", ensemble ) );
    const auto model = ModelCatalog::instance().find( "ens-self" );
    REQUIRE( model.has_value() );
    CHECK( model->readiness == ModelReadiness::InvalidManifest );
    CHECK( model->readinessReason.find( "the ensemble itself" ) != std::string::npos );
  }
}

TEST_CASE( "weighted_mean combines member probability stacks with weights",
           "[models][ensemble][run]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "ensfw-a", 2.0 );
  const ScaleProviderGuard guardB( "ensfw-b", 0.5 );
  QTemporaryDir dir;
  registerMember( "member-a", "ensfw-a", dir );
  registerMember( "member-b", "ensfw-b", dir );
  registerEnsemble( dir, "ens-test" );

  const QString input = writeConstantRaster( dir, "input.tif", 32, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "mean.tif" ) );

  ModelExecutionRequest request = rasterRequest( input.toStdString(), output.toStdString(), "ens-test" );
  RSOperatorContext context;
  ModelExecutionResult result;
  REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );

  // Member A: 10×2 = 20; member B: 10×0.5 = 5; equal weights → mean 12.5.
  bool ok = false;
  const float v = readPixel( output, 1, 5, 5, &ok );
  REQUIRE( ok );
  CHECK( v == Catch::Approx( 12.5f ).margin( 1e-4f ) );
  CHECK( result.rasterStats.outBands == 3 ); // 2 class planes + variance band
  CHECK( result.payload["combination"].asString() == "weighted_mean" );
  CHECK( result.payload["ensemble_members"].size() == 2 );
  CHECK( result.payload["backend"].asString() == "ensemble(weighted_mean)" );

  // Uncertainty (auto → variance): members at 12.5±7.5 → variance 56.25.
  const float unc = readPixel( output, 3, 5, 5, &ok );
  REQUIRE( ok );
  CHECK( unc == Catch::Approx( 56.25f ).margin( 1e-3f ) );

  // The provenance sidecar carries the member identities and passes the
  // consumer-side verification against the ensemble reference.
  const auto verdict = verifyProductAgainstModel( output.toStdString(), "ens-test" );
  CHECK( verdict.state == sicnu::operators::runtime::ProvenanceVerdict::State::Ok );
  REQUIRE( verdict.provenance["ensemble"].isObject() );
  CHECK( verdict.provenance["ensemble"]["members"].size() == 2 );
  CHECK( verdict.provenance["ensemble"]["combination"].asString() == "weighted_mean" );
  CHECK( verdict.provenance["ensemble"]["members"][0]["backend"].asString() == "scale_backend" );
}

TEST_CASE( "explicit weights shift the weighted mean", "[models][ensemble][run]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "ensfw-a", 2.0 );
  const ScaleProviderGuard guardB( "ensfw-b", 0.5 );
  QTemporaryDir dir;
  registerMember( "member-a", "ensfw-a", dir );
  registerMember( "member-b", "ensfw-b", dir );
  // weights 3:1 → (20×3 + 5×1) / 4 = 16.25 on the constant-10 input.
  registerEnsemble( dir, "ens-test", 3.0, 1.0, std::string(), "none" );

  const QString input = writeConstantRaster( dir, "input.tif", 16, 1, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "weighted.tif" ) );
  ModelExecutionRequest request = rasterRequest( input.toStdString(), output.toStdString(), "ens-test" );
  RSOperatorContext context;
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );

  bool ok = false;
  const float v = readPixel( output, 1, 3, 3, &ok );
  REQUIRE( ok );
  CHECK( v == Catch::Approx( 16.25f ).margin( 1e-4f ) );
}

TEST_CASE( "weighted_vote resolves ties to the lowest class and honors weights",
           "[models][ensemble][vote]" )
{
  RegistryReset reset;
  // Member A (×1) on the two-band input [1, 3] → argmax class 1.
  // Member B (×-1) negates to [-1, -3] → argmax class 0.
  const ScaleProviderGuard guardA( "ensfw-a", 1.0 );
  const ScaleProviderGuard guardB( "ensfw-b", -1.0 );
  QTemporaryDir dir;
  registerMember( "vote-a", "ensfw-a", dir );
  registerMember( "vote-b", "ensfw-b", dir );

  Json::Value json( Json::objectValue );
  json["name"] = "ens-vote";
  json["task"] = "segmentation";
  json["framework"] = "onnx";
  json["output"]["classes"].append( "bg" );
  json["output"]["classes"].append( "fore" );
  Json::Value &ensemble = json["ensemble"] = Json::Value( Json::objectValue );
  Json::Value members( Json::arrayValue );
  Json::Value a( Json::objectValue );
  a["model"] = "vote-a";
  a["weight"] = 3.0;
  members.append( a );
  Json::Value b( Json::objectValue );
  b["model"] = "vote-b";
  b["weight"] = 1.0;
  members.append( b );
  ensemble["members"] = members;
  ensemble["combination"] = "weighted_vote";
  ensemble["uncertainty"] = "agreement";
  std::string error;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ),
    dir.filePath( QStringLiteral( "vote/model.json" ) ).toStdString(), &error ) );

  // Two-band raster: band 1 = 1, band 2 = 3.
  const QString input = dir.filePath( QStringLiteral( "vote_input.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 16, 16, 2, GDT_Float32 );
  builder.withConstantValue( 1, 1.0f ).withConstantValue( 2, 3.0f );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "vote_out.tif" ) );

  ModelExecutionRequest request = rasterRequest( input.toStdString(), output.toStdString(), "ens-vote" );
  RSOperatorContext context;
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );

  bool ok = false;
  // Votes: class1 (member A) weight 3 vs class0 (member B) weight 1 → label 1.
  const float label = readPixel( output, 1, 4, 4, &ok );
  REQUIRE( ok );
  CHECK( label == 1.0f );
  // Agreement = winning vote share 3/4 = 0.75, quantized to Byte with the
  // sentinel excluded: round(0.75 × 254) = 191.
  const float agreement = readPixel( output, 2, 4, 4, &ok );
  REQUIRE( ok );
  CHECK( agreement == Catch::Approx( 191.0f ).margin( 1e-3f ) );
  CHECK( rasterBandCount( output ) == 2 );
}

TEST_CASE( "ensemble failures are atomic — no partial output, no residue",
           "[models][ensemble][failure]" )
{
  RegistryReset reset;
  // Member B's provider fails to load → the ensemble must fail BEFORE any
  // output exists.
  const ScaleProviderGuard guardA( "ensfw-a", 2.0 );
  ModelRuntimeRegistry::instance().registerProvider(
    "ensfw-b",
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string *error ) -> ModelRuntimePtr {
      if ( error )
        *error = "member-b load failure (injected)";
      return nullptr;
    } );
  QTemporaryDir dir;
  registerMember( "member-a", "ensfw-a", dir );
  registerMember( "member-b", "ensfw-b", dir );
  registerEnsemble( dir, "ens-fail", 1.0, 1.0, std::string(), "none" );

  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "never.tif" ) );
  ModelExecutionRequest request = rasterRequest( input.toStdString(), output.toStdString(), "ens-fail" );
  RSOperatorContext context;
  REQUIRE_THROWS_AS( sicnu::operators::runtime::runModelInference( request, context ),
                     RSOperatorError );

  CHECK_FALSE( QFile::exists( output ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".prov.json" ) ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".tmp~" ) ) );
  // No member residue either — the stacks AND the member provenance
  // sidecars the member engines publish next to them (Platform 13.0 residue
  // fix: 12.0 tracked only the stacks).
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( ".never.tif.ensemble-member0.tmp~" ) ) ) );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( ".never.tif.ensemble-member1.tmp~" ) ) ) );
  CHECK_FALSE( QFile::exists(
    dir.filePath( QStringLiteral( ".never.tif.ensemble-member0.tmp~.prov.json" ) ) ) );
  CHECK_FALSE( QFile::exists(
    dir.filePath( QStringLiteral( ".never.tif.ensemble-member1.tmp~.prov.json" ) ) ) );
}

TEST_CASE( "ensemble channel-count disagreement is a typed refusal",
           "[models][ensemble][failure]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "ensfw-a", 2.0 );
  // Member B emits one channel fewer than it receives — the stacks disagree
  // post-run, which must be a typed refusal, never a partial combination.
  ModelRuntimeRegistry::instance().registerProvider(
    "ensfw-b",
    []( const ModelInfo &model, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      const std::string artifact = model.resolvedArtifactPath;
      struct Dropping final : IModelRuntime
      {
        Dropping( std::string artifact ) : m_artifact( std::move( artifact ) ) {}
        cv::Mat infer( const cv::Mat &blob ) override
        {
          std::vector<int> shape( static_cast<std::size_t>( blob.dims ), 0 );
          for ( int d = 0; d < blob.dims; ++d )
            shape[static_cast<std::size_t>( d )] = blob.size[d];
          shape[1] = std::max( 1, blob.size[1] - 1 );
          return cv::Mat( blob.dims, shape.data(), CV_32F, cv::Scalar( 1.0f ) ).clone();
        }
        std::string framework() const override { return "ensfw-b"; }
        std::string backendName() const override { return "dropping"; }
        std::string deviceName() const override { return "cpu"; }
        std::string artifactPath() const override { return m_artifact; }
        std::string m_artifact;
      };
      return std::make_shared<Dropping>( artifact );
    } );
  QTemporaryDir dir;
  registerMember( "member-a", "ensfw-a", dir );
  registerMember( "member-b", "ensfw-b", dir );
  registerEnsemble( dir, "ens-channels", 1.0, 1.0, std::string(), "none" );

  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "channels.tif" ) );
  ModelExecutionRequest request = rasterRequest( input.toStdString(), output.toStdString(), "ens-channels" );
  RSOperatorContext context;
  REQUIRE_THROWS_AS( sicnu::operators::runtime::runModelInference( request, context ),
                     RSOperatorError );
  CHECK_FALSE( QFile::exists( output ) );
}

TEST_CASE( "ensemble surface exclusions refuse loudly", "[models][ensemble][refusal]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "ensfw-a", 2.0 );
  const ScaleProviderGuard guardB( "ensfw-b", 0.5 );
  QTemporaryDir dir;
  registerMember( "member-a", "ensfw-a", dir );
  registerMember( "member-b", "ensfw-b", dir );
  registerEnsemble( dir, "ens-refuse", 1.0, 1.0, std::string(), "none" );

  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );
  RSOperatorContext context;

  // Platform 13.0: detection decode now HAS a defined combination ("wbf").
  // A detection request on a probability-combination ensemble is still a
  // typed refusal — the combination token states the product semantics.
  SECTION( "detection decode on a non-wbf ensemble" )
  {
    ModelExecutionRequest request =
      rasterRequest( input.toStdString(), dir.filePath( QStringLiteral( "d.tif" ) ).toStdString(), "ens-refuse" );
    request.asDetection = true;
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "requires combination 'wbf'" ) );
  }

  // Platform 13.0: scene classification now HAS a defined combination over
  // per-class probability vectors. These members declare no output.classes,
  // so the shared-vocabulary contract still refuses — loudly.
  SECTION( "scene classification without a class vocabulary" )
  {
    ModelExecutionRequest request =
      rasterRequest( input.toStdString(), dir.filePath( QStringLiteral( "c.json" ) ).toStdString(), "ens-refuse" );
    request.asSceneClassification = true;
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "no output.classes" ) );
  }

  // Platform 13.0: request-level derived modes are defined over the
  // weighted MEAN (argmax/top-1/threshold). Over weighted VOTES they stay
  // refused — the vote product IS the label product.
  SECTION( "derived output mode on a vote ensemble" )
  {
    Json::Value voteEnsemble( Json::objectValue );
    voteEnsemble["name"] = "ens-refuse-vote";
    voteEnsemble["task"] = "segmentation";
    voteEnsemble["framework"] = "onnx";
    Json::Value voteMembers( Json::arrayValue );
    voteMembers.append( "member-a" );
    voteMembers.append( "member-b" );
    voteEnsemble["ensemble"]["members"] = voteMembers;
    voteEnsemble["ensemble"]["combination"] = "weighted_vote";
    voteEnsemble["ensemble"]["uncertainty"] = "agreement";
    std::string voteError;
    REQUIRE( ModelCatalog::instance().registerManifestJson(
      Json::writeString( Json::StreamWriterBuilder(), voteEnsemble ),
      dir.filePath( QStringLiteral( "refuse-vote/model.json" ) ).toStdString(), &voteError ) );

    ModelExecutionRequest request =
      rasterRequest( input.toStdString(), dir.filePath( QStringLiteral( "l.tif" ) ).toStdString(), "ens-refuse-vote" );
    request.outputMode = RasterOutputMode::Mask;
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "undefined over weighted votes" ) );
  }

  SECTION( "multi-feed request" )
  {
    ModelExecutionRequest request =
      rasterRequest( input.toStdString(), dir.filePath( QStringLiteral( "m.tif" ) ).toStdString(), "ens-refuse" );
    NamedRasterFeed feed;
    feed.name = "x";
    feed.paths = { input.toStdString() };
    request.namedInputs = { feed };
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "multi-feed" ) );
  }
}

TEST_CASE( "combine pass handles multi-block and tail blocks with an identical product",
           "[models][ensemble][run][blocks]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "ensfw-a", 2.0 );
  const ScaleProviderGuard guardB( "ensfw-b", 0.5 );
  QTemporaryDir dir;
  registerMember( "member-a", "ensfw-a", dir );
  registerMember( "member-b", "ensfw-b", dir );
  registerEnsemble( dir, "ens-blocks", 1.0, 1.0, std::string(), "variance" );

  // 16x16 constant raster; the shrunken budget forces rowsPerBlock=1, so the
  // pass runs 16 single-row blocks and the row-band channel offsets are
  // exercised on every iteration (the last block is the tail).
  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "blocks.tif" ) );

  qputenv( "SICNU_ENSEMBLE_BLOCK_VALUES", "8" ); // 8 floats << members*channels*width
  ModelExecutionRequest request = rasterRequest( input.toStdString(), output.toStdString(), "ens-blocks" );
  RSOperatorContext context;
  ModelExecutionResult result;
  REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );
  qputenv( "SICNU_ENSEMBLE_BLOCK_VALUES", "" );

  bool ok = false;
  const float v = readPixel( output, 1, 2, 15, &ok ); // last row, tail block
  REQUIRE( ok );
  CHECK( v == Catch::Approx( 12.5f ).margin( 1e-4f ) );
  const float unc = readPixel( output, 3, 2, 15, &ok );
  REQUIRE( ok );
  CHECK( unc == Catch::Approx( 56.25f ).margin( 1e-3f ) );
  CHECK( result.rasterStats.outHeight == 16 );
}

TEST_CASE( "member NaN poisons the combined pixel across every band",
           "[models][ensemble][nodata]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "ensfw-a", 1.0 );
  // Member B NaNs out exactly one pixel (2,2) of every channel plane.
  ModelRuntimeRegistry::instance().registerProvider(
    "ensfw-b",
    []( const ModelInfo &model, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      const std::string artifact = model.resolvedArtifactPath;
      struct SparseNaN final : IModelRuntime
      {
        SparseNaN( std::string artifact ) : m_artifact( std::move( artifact ) ) {}
        cv::Mat infer( const cv::Mat &blob ) override
        {
          cv::Mat out = blob.clone();
          // Continuous (1, C, H, W): flat NCHW indexing — row-pointer math
          // does not apply to rank-4 Mats.
          const int channels = blob.size[1];
          const int h = blob.size[2];
          const int w = blob.size[3];
          float *data = reinterpret_cast<float *>( out.data );
          for ( int c = 0; c < channels; ++c )
            data[static_cast<std::size_t>( c ) * h * w + 2 * w + 2] =
              std::numeric_limits<float>::quiet_NaN();
          return out;
        }
        std::string framework() const override { return "ensfw-b"; }
        std::string backendName() const override { return "sparse_nan"; }
        std::string deviceName() const override { return "cpu"; }
        std::string artifactPath() const override { return m_artifact; }
        std::string m_artifact;
      };
      return std::make_shared<SparseNaN>( artifact );
    } );
  QTemporaryDir dir;
  registerMember( "member-a", "ensfw-a", dir );
  registerMember( "member-b", "ensfw-b", dir );
  registerEnsemble( dir, "ens-nan", 1.0, 1.0, std::string(), "variance" );

  const QString input = writeConstantRaster( dir, "input.tif", 8, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "nan.tif" ) );
  ModelExecutionRequest request = rasterRequest( input.toStdString(), output.toStdString(), "ens-nan" );
  RSOperatorContext context;
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );

  bool ok = false;
  const float poisoned = readPixel( output, 1, 2, 2, &ok );
  REQUIRE( ok );
  CHECK( std::isnan( poisoned ) );
  const float poisonedUnc = readPixel( output, 3, 2, 2, &ok );
  REQUIRE( ok );
  CHECK( std::isnan( poisonedUnc ) );
  // Neighbours stay clean.
  const float clean = readPixel( output, 1, 3, 2, &ok );
  REQUIRE( ok );
  CHECK( clean == Catch::Approx( 10.0f ).margin( 1e-4f ) );
}

TEST_CASE( "a successful ensemble run leaves no member residue behind",
           "[models][ensemble][failure]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "ensfw-a", 2.0 );
  const ScaleProviderGuard guardB( "ensfw-b", 0.5 );
  QTemporaryDir dir;
  registerMember( "member-a", "ensfw-a", dir );
  registerMember( "member-b", "ensfw-b", dir );
  registerEnsemble( dir, "ens-clean", 1.0, 1.0, std::string(), "none" );

  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "clean.tif" ) );
  ModelExecutionRequest request = rasterRequest( input.toStdString(), output.toStdString(), "ens-clean" );
  RSOperatorContext context;
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );
  REQUIRE( QFile::exists( output ) );

  // The success path must clean up every staged member stack AND its
  // provenance sidecar (Platform 13.0 residue fix).
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( ".clean.tif.ensemble-member0.tmp~" ) ) ) );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( ".clean.tif.ensemble-member1.tmp~" ) ) ) );
  CHECK_FALSE( QFile::exists(
    dir.filePath( QStringLiteral( ".clean.tif.ensemble-member0.tmp~.prov.json" ) ) ) );
  CHECK_FALSE( QFile::exists(
    dir.filePath( QStringLiteral( ".clean.tif.ensemble-member1.tmp~.prov.json" ) ) ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".tmp~" ) ) );
}

TEST_CASE( "all-zero-weight ensembles refuse before any member forward (hardening 15/20)",
           "[models][ensemble][weights][p15]" )
{
  RegistryReset reset;
  const CountingProviderGuard guardA( "cntfw-a" );
  const CountingProviderGuard guardB( "cntfw-b" );
  QTemporaryDir dir;
  registerMember( "cnt-a", "cntfw-a", dir );
  registerMember( "cnt-b", "cntfw-b", dir );
  // Hand-built manifest: the shared registerEnsemble() helper references the
  // hardcoded members "member-a"/"member-b", which would make the ensemble
  // unresolvable and refuse BEFORE the weight gate for an unrelated reason.
  {
    Json::Value json( Json::objectValue );
    json["name"] = "cnt-ens-zero";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    Json::Value &ensemble = json["ensemble"] = Json::Value( Json::objectValue );
    Json::Value members( Json::arrayValue );
    Json::Value a( Json::objectValue );
    a["model"] = "cnt-a";
    a["weight"] = 0.0;
    members.append( a );
    Json::Value b( Json::objectValue );
    b["model"] = "cnt-b";
    b["weight"] = 0.0;
    members.append( b );
    ensemble["members"] = members;
    std::string error;
    const bool ok = ModelCatalog::instance().registerManifestJson(
      Json::writeString( Json::StreamWriterBuilder(), json ),
      dir.filePath( QStringLiteral( "cnt-ens-zero/model.json" ) ).toStdString(), &error );
    if ( !ok )
      FAIL( "ensemble rejected: " + error );
  }

  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "zero.tif" ) );
  RSOperatorContext context;
  // Statically undefined manifest: the typed refusal must fire BEFORE any
  // member session is acquired or run — the members used to be fully
  // executed (VRAM reserved, full raster passes) before the combine pass
  // noticed the zero weight sum.
  REQUIRE_THROWS_AS( sicnu::operators::runtime::runModelInference(
                       rasterRequest( input.toStdString(), output.toStdString(),
                                      "cnt-ens-zero" ),
                       context ),
                     RSOperatorError );
  CHECK( guardA.forwards->load() == 0 );
  CHECK( guardB.forwards->load() == 0 );
  CHECK_FALSE( QFile::exists( output ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".prov.json" ) ) );
}
