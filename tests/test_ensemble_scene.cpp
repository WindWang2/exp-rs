// tests/test_ensemble_scene.cpp — scene-classification ensembles and derived
// output modes (Platform 13.0): hand-computed weighted_mean / weighted_vote
// oracles over the members' per-class probability vectors, the strict class-
// vocabulary contract (mismatch → typed refusal), the exp-rs-classification/1
// artifact with its additive `ensemble` block, request-level derived outputs
// on weighted_mean (labels/confidence/mask), and the machine-independent
// staging-compression evidence (deflate vs none: identical values, fewer
// bytes).
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
#include "synthetic_raster_builder.h"

#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelExecutionResult;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::RasterOutputMode;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

/// Scene head fake: returns a fixed logit row (1, classCount) per framework.
class SceneLogitsRuntime final : public IModelRuntime
{
  public:
    SceneLogitsRuntime( std::string artifact, std::string framework,
                        std::vector<double> logits )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_logits( std::move( logits ) ) {}

    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "scene_fake"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }

    cv::Mat infer( const cv::Mat & ) override
    {
      cv::Mat out( 1, static_cast<int>( m_logits.size() ), CV_32F );
      for ( std::size_t i = 0; i < m_logits.size(); ++i )
        out.ptr<float>( 0 )[i] = static_cast<float>( m_logits[i] );
      return out;
    }

  private:
    std::string m_artifact;
    std::string m_framework;
    std::vector<double> m_logits;
};

struct SceneProviderGuard
{
  SceneProviderGuard( const std::string &framework, std::vector<double> logits )
  {
    ModelRuntimeRegistry::instance().registerProvider(
      framework,
      [ framework, logits = std::move( logits ) ]( const ModelInfo &model,
                                                   const ModelHardwareCapabilities &,
                                                   std::string *error ) -> ModelRuntimePtr {
        if ( model.resolvedArtifactPath.empty() )
        {
          if ( error )
            *error = "no resolved artifact";
          return nullptr;
        }
        return std::make_shared<SceneLogitsRuntime>( model.resolvedArtifactPath, framework,
                                                     logits );
      } );
  }
};

/// Scale provider (raster ensembles / derived-mode evidence).
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

void registerManifest( const Json::Value &json, const std::string &manifestPath )
{
  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ), manifestPath, &error );
  if ( !ok )
    FAIL( "manifest rejected: " + error );
}

Json::Value sceneMemberManifest( const std::string &name, const std::string &framework,
                                 const std::vector<std::string> &classes,
                                 const std::string &confidence = std::string() )
{
  Json::Value json( Json::objectValue );
  json["name"] = name;
  json["task"] = "classification";
  json["framework"] = framework;
  json["artifact"]["path"] = identityModelPath().toStdString();
  Json::Value classesJson( Json::arrayValue );
  for ( const std::string &cls : classes )
    classesJson.append( cls );
  json["output"]["classes"] = classesJson;
  if ( !confidence.empty() )
  {
    json["output"]["heads"][0]["role"] = "classification";
    json["output"]["heads"][0]["confidence"] = confidence;
  }
  return json;
}

Json::Value rasterMemberManifest( const std::string &name, const std::string &framework )
{
  Json::Value json( Json::objectValue );
  json["name"] = name;
  json["task"] = "segmentation";
  json["framework"] = framework;
  json["artifact"]["path"] = identityModelPath().toStdString();
  return json;
}

Json::Value readJson( const QString &path )
{
  std::ifstream stream( path.toStdString() );
  REQUIRE( stream.is_open() );
  Json::CharReaderBuilder builder;
  Json::Value value;
  std::string errors;
  REQUIRE( Json::parseFromStream( builder, stream, &value, &errors ) );
  return value;
}

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

float readPixel( const QString &path, int band, int x, int y )
{
  GDALDataset *ds = GDALDataset::Open( path.toUtf8().constData(), GA_ReadOnly );
  if ( !ds )
    return 0.0f;
  float value = 0.0f;
  ds->GetRasterBand( band )->RasterIO( GF_Read, x, y, 1, 1, &value, 1, 1, GDT_Float32, 0, 0 );
  GDALClose( ds );
  return value;
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

} // namespace

// ---------------------------------------------------------------------------
// Scene-classification ensemble
// ---------------------------------------------------------------------------

TEST_CASE( "scene ensemble combines member probabilities with weights",
           "[models][ensemble][scene]" )
{
  RegistryReset reset;
  // Member A logits [2, 1] → softmax [0.7310586, 0.2689414]
  // Member B logits [0.5, 2.5] → softmax [0.1192030, 0.8807970]
  const SceneProviderGuard guardA( "scfw-a", { 2.0, 1.0 } );
  const SceneProviderGuard guardB( "scfw-b", { 0.5, 2.5 } );
  QTemporaryDir dir;
  registerManifest( sceneMemberManifest( "scene-a", "scfw-a", { "water", "land" }, "logit" ),
                    dir.filePath( QStringLiteral( "scene-a/model.json" ) ).toStdString() );
  registerManifest( sceneMemberManifest( "scene-b", "scfw-b", { "water", "land" }, "logit" ),
                    dir.filePath( QStringLiteral( "scene-b/model.json" ) ).toStdString() );

  Json::Value ensemble( Json::objectValue );
  ensemble["name"] = "scene-ens";
  ensemble["task"] = "classification";
  ensemble["framework"] = "onnx";
  Json::Value members( Json::arrayValue );
  Json::Value a( Json::objectValue );
  a["model"] = "scene-a";
  a["weight"] = 3.0;
  members.append( a );
  Json::Value b( Json::objectValue );
  b["model"] = "scene-b";
  b["weight"] = 1.0;
  members.append( b );
  ensemble["ensemble"]["members"] = members;
  registerManifest( ensemble, dir.filePath( QStringLiteral( "scene-ens/model.json" ) ).toStdString() );

  const QString input = writeConstantRaster( dir, "scene_input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "scene.json" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "scene-ens";
  request.asSceneClassification = true;
  RSOperatorContext context;
  ModelExecutionResult result;
  REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );

  CHECK( result.backend == "ensemble(weighted_mean)" );
  const Json::Value doc = readJson( output );
  CHECK( doc["schema"].asString() == "exp-rs-classification/1" );
  CHECK( doc["score_semantics"].asString() == "ensemble_weighted_mean" );
  // weighted mean with weights 3:1:
  //   water = (3·0.7310586 + 1·0.1192030)/4 = 0.5780947
  //   land  = (3·0.2689414 + 1·0.8807970)/4 = 0.4219053
  CHECK( doc["probabilities"]["water"].asDouble() == Catch::Approx( 0.5780947 ).margin( 1e-6 ) );
  CHECK( doc["probabilities"]["land"].asDouble() == Catch::Approx( 0.4219053 ).margin( 1e-6 ) );
  CHECK( doc["predicted_index"].asInt() == 0 );
  CHECK( doc["predicted_class"].asString() == "water" );
  // The ensemble block names every member's identity, weight and semantics.
  REQUIRE( doc["ensemble"].isObject() );
  CHECK( doc["ensemble"]["combination"].asString() == "weighted_mean" );
  REQUIRE( doc["ensemble"]["members"].size() == 2 );
  CHECK( doc["ensemble"]["members"][0]["weight"].asDouble() == Catch::Approx( 3.0 ) );
  CHECK( doc["ensemble"]["members"][0]["score_semantics"].asString() == "logit" );
  CHECK( doc["ensemble"]["members"][1]["backend"].asString() == "scene_fake" );
  CHECK( doc["model"]["identity_tag"].asString() == "scene-ens@0" );
  CHECK( result.payload["ensemble_members"].size() == 2 );
  // Scene evidence: 16x16, 2 bands.
  CHECK( doc["scene"]["width"].asInt() == 16 );
  CHECK( doc["scene"]["bands"].asInt() == 2 );
}

TEST_CASE( "scene ensemble weighted_vote resolves ties to the lowest class",
           "[models][ensemble][scene][vote]" )
{
  RegistryReset reset;
  const SceneProviderGuard guardA( "vscfw-a", { 2.0, 1.0 } );  // votes water
  const SceneProviderGuard guardB( "vscfw-b", { 0.5, 2.5 } );  // votes land
  QTemporaryDir dir;
  registerManifest( sceneMemberManifest( "vscene-a", "vscfw-a", { "water", "land" }, "logit" ),
                    dir.filePath( QStringLiteral( "vscene-a/model.json" ) ).toStdString() );
  registerManifest( sceneMemberManifest( "vscene-b", "vscfw-b", { "water", "land" }, "logit" ),
                    dir.filePath( QStringLiteral( "vscene-b/model.json" ) ).toStdString() );

  Json::Value ensemble( Json::objectValue );
  ensemble["name"] = "vscene-ens";
  ensemble["task"] = "classification";
  ensemble["framework"] = "onnx";
  Json::Value members( Json::arrayValue );
  members.append( "vscene-a" );
  members.append( "vscene-b" );
  ensemble["ensemble"]["members"] = members;
  ensemble["ensemble"]["combination"] = "weighted_vote";
  registerManifest( ensemble,
                    dir.filePath( QStringLiteral( "vscene-ens/model.json" ) ).toStdString() );

  const QString input = writeConstantRaster( dir, "scene_input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "vscene.json" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "vscene-ens";
  request.asSceneClassification = true;
  RSOperatorContext context;
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );

  const Json::Value doc = readJson( output );
  // Votes 1:1 → tie → LOWEST class index (water). agreement = 0.5.
  CHECK( doc["score_semantics"].asString() == "ensemble_weighted_vote" );
  CHECK( doc["predicted_index"].asInt() == 0 );
  CHECK( doc["predicted_class"].asString() == "water" );
  CHECK( doc["agreement"].asDouble() == Catch::Approx( 0.5 ).margin( 1e-12 ) );
  // The probabilities field still carries the (well-defined) weighted mean.
  CHECK( doc["probabilities"]["water"].asDouble() == Catch::Approx( 0.4251308 ).margin( 1e-6 ) );
}

TEST_CASE( "scene ensemble refusals stay typed", "[models][ensemble][scene][refusal]" )
{
  RegistryReset reset;
  const SceneProviderGuard guardA( "rscfw-a", { 2.0, 1.0 } );
  QTemporaryDir dir;
  registerManifest( sceneMemberManifest( "rscene-a", "rscfw-a", { "water", "land" }, "logit" ),
                    dir.filePath( QStringLiteral( "rscene-a/model.json" ) ).toStdString() );
  // Vocabulary mismatch: three classes.
  registerManifest( sceneMemberManifest( "rscene-b", "rscfw-a", { "water", "land", "bare" }, "logit" ),
                    dir.filePath( QStringLiteral( "rscene-b/model.json" ) ).toStdString() );
  // No declared classes at all.
  registerManifest( sceneMemberManifest( "rscene-c", "rscfw-a", {} ),
                    dir.filePath( QStringLiteral( "rscene-c/model.json" ) ).toStdString() );
  // Distance semantics: not a classification distribution.
  registerManifest( sceneMemberManifest( "rscene-d", "rscfw-a", { "water", "land" }, "distance" ),
                    dir.filePath( QStringLiteral( "rscene-d/model.json" ) ).toStdString() );

  auto registerEnsemble = [ & ]( const std::string &name, const std::string &memberA,
                                 const std::string &memberB ) {
    Json::Value ensemble( Json::objectValue );
    ensemble["name"] = name;
    ensemble["task"] = "classification";
    ensemble["framework"] = "onnx";
    Json::Value members( Json::arrayValue );
    members.append( memberA );
    members.append( memberB );
    ensemble["ensemble"]["members"] = members;
    registerManifest( ensemble, dir.filePath( QString::fromStdString( name ) + "/model.json" )
                                  .toStdString() );
  };

  const QString input = writeConstantRaster( dir, "scene_input.tif", 16, 2, 10.0f );
  RSOperatorContext context;
  auto requestFor = [ & ]( const std::string &model, const QString &out ) {
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = out.toStdString();
    request.modelReference = model;
    request.asSceneClassification = true;
    return request;
  };

  SECTION( "class vocabulary mismatch across members" )
  {
    registerEnsemble( "rscene-vocab", "rscene-a", "rscene-b" );
    REQUIRE_THROWS_WITH(
      sicnu::operators::runtime::runModelInference(
        requestFor( "rscene-vocab", dir.filePath( QStringLiteral( "v.json" ) ) ), context ),
      Catch::Matchers::ContainsSubstring( "shared vocabulary" ) );
    CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "v.json" ) ) ) );
  }

  SECTION( "a member without output.classes" )
  {
    registerEnsemble( "rscene-noclass", "rscene-a", "rscene-c" );
    REQUIRE_THROWS_WITH(
      sicnu::operators::runtime::runModelInference(
        requestFor( "rscene-noclass", dir.filePath( QStringLiteral( "n.json" ) ) ), context ),
      Catch::Matchers::ContainsSubstring( "no output.classes" ) );
    CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "n.json" ) ) ) );
  }

  SECTION( "distance confidence semantics" )
  {
    registerEnsemble( "rscene-dist", "rscene-a", "rscene-d" );
    REQUIRE_THROWS_WITH(
      sicnu::operators::runtime::runModelInference(
        requestFor( "rscene-dist", dir.filePath( QStringLiteral( "d.json" ) ) ), context ),
      Catch::Matchers::ContainsSubstring( "distance" ) );
    CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "d.json" ) ) ) );
  }

  SECTION( "scene classification on a wbf ensemble" )
  {
    Json::Value ensemble( Json::objectValue );
    ensemble["name"] = "rscene-wbf";
    ensemble["task"] = "classification";
    ensemble["framework"] = "onnx";
    Json::Value members( Json::arrayValue );
    members.append( "rscene-a" );
    members.append( "rscene-b" );
    ensemble["ensemble"]["members"] = members;
    ensemble["ensemble"]["combination"] = "wbf";
    registerManifest( ensemble,
                      dir.filePath( QStringLiteral( "rscene-wbf/model.json" ) ).toStdString() );
    // The generic routing refusal fires first: a wbf manifest serves
    // detection requests only (the message names the fix).
    REQUIRE_THROWS_WITH(
      sicnu::operators::runtime::runModelInference(
        requestFor( "rscene-wbf", dir.filePath( QStringLiteral( "w.json" ) ) ), context ),
      Catch::Matchers::ContainsSubstring( "not a detection run" ) );
  }
}

// ---------------------------------------------------------------------------
// Derived output modes on raster ensembles
// ---------------------------------------------------------------------------

TEST_CASE( "ensemble derived output modes collapse the combined mean",
           "[models][ensemble][derived]" )
{
  RegistryReset reset;
  // Members scale the 2-band constant input [1, 3]:
  //   A ×2 → [2, 6]; B ×0.5 → [0.5, 1.5]
  //   combined mean = [1.25, 3.75]
  const ScaleProviderGuard guardA( "derfw-a", 2.0 );
  const ScaleProviderGuard guardB( "derfw-b", 0.5 );
  QTemporaryDir dir;
  registerManifest( rasterMemberManifest( "der-a", "derfw-a" ),
                    dir.filePath( QStringLiteral( "der-a/model.json" ) ).toStdString() );
  registerManifest( rasterMemberManifest( "der-b", "derfw-b" ),
                    dir.filePath( QStringLiteral( "der-b/model.json" ) ).toStdString() );

  auto registerEnsemble = [ & ]( const std::string &name, const std::string &maskThreshold ) {
    Json::Value ensemble( Json::objectValue );
    ensemble["name"] = name;
    ensemble["task"] = "segmentation";
    ensemble["framework"] = "onnx";
    Json::Value members( Json::arrayValue );
    members.append( "der-a" );
    members.append( "der-b" );
    ensemble["ensemble"]["members"] = members;
    ensemble["ensemble"]["uncertainty"] = "none";
    if ( !maskThreshold.empty() )
      ensemble["postprocess"]["mask_threshold"] = std::stod( maskThreshold );
    registerManifest( ensemble, dir.filePath( QString::fromStdString( name ) + "/model.json" )
                                  .toStdString() );
  };

  sicnu::testing::RsSyntheticRasterBuilder builder( 16, 16, 2, GDT_Float32 );
  builder.withConstantValue( 1, 1.0f ).withConstantValue( 2, 3.0f );
  const QString input = dir.filePath( QStringLiteral( "der_input.tif" ) );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );
  RSOperatorContext context;

  SECTION( "labels: argmax of the combined mean" )
  {
    registerEnsemble( "der-labels", std::string() );
    const QString output = dir.filePath( QStringLiteral( "labels.tif" ) );
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = output.toStdString();
    request.modelReference = "der-labels";
    request.outputMode = RasterOutputMode::Labels;
    ModelExecutionResult result;
    REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );
    CHECK( result.rasterStats.outBands == 1 );
    // argmax([1.25, 3.75]) = class 1 (a STRICT maximum).
    CHECK( readPixel( output, 1, 4, 4 ) == 1.0f );
    CHECK( rasterBandCount( output ) == 1 );
    // The per-class tally covers the whole raster.
    REQUIRE( result.payload["classPixelCounts"].size() == 2 );
    CHECK( result.payload["classPixelCounts"][0].asInt64() == 0 );
    CHECK( result.payload["classPixelCounts"][1].asInt64() == 16 * 16 );
  }

  SECTION( "labels: an exact tie resolves to the lowest class" )
  {
    registerEnsemble( "der-labels-tie", std::string() );
    // A tie raster: both channels carry 1, so the combined mean is [1.25, 1.25]
    // (members scale by 2 and 0.5) — argmax must resolve to class 0.
    const QString tieInput = dir.filePath( QStringLiteral( "tie_input.tif" ) );
    sicnu::testing::RsSyntheticRasterBuilder tieBuilder( 16, 16, 2, GDT_Float32 );
    tieBuilder.withConstantValue( 1, 1.0f ).withConstantValue( 2, 1.0f );
    tieBuilder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( tieInput );
    const QString output = dir.filePath( QStringLiteral( "labels_tie.tif" ) );
    ModelExecutionRequest request;
    request.inputPath = tieInput.toStdString();
    request.outputPath = output.toStdString();
    request.modelReference = "der-labels-tie";
    request.outputMode = RasterOutputMode::Labels;
    REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );
    CHECK( readPixel( output, 1, 4, 4 ) == 0.0f );
    CHECK( readPixel( output, 1, 0, 0 ) == 0.0f );
  }

  SECTION( "confidence: top-1 combined probability" )
  {
    registerEnsemble( "der-confidence", std::string() );
    const QString output = dir.filePath( QStringLiteral( "conf.tif" ) );
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = output.toStdString();
    request.modelReference = "der-confidence";
    request.outputMode = RasterOutputMode::Confidence;
    REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );
    CHECK( readPixel( output, 1, 4, 4 ) == Catch::Approx( 3.75f ).margin( 1e-5f ) );
  }

  SECTION( "mask: multi-channel threshold is the argmax!=background rule" )
  {
    registerEnsemble( "der-mask", "2.0" );
    const QString output = dir.filePath( QStringLiteral( "mask.tif" ) );
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = output.toStdString();
    request.modelReference = "der-mask";
    request.outputMode = RasterOutputMode::Mask;
    REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, context ) );
    // argmax class 1 != 0 → 1.
    CHECK( readPixel( output, 1, 4, 4 ) == 1.0f );
  }

  SECTION( "mask: single-channel threshold gates the combined mean" )
  {
    // One channel: the mask IS the threshold comparison of the combined mean.
    // Members scale the constant 1.0 by 2 and 0.5 → mean 1.25.
    const QString singleInput = dir.filePath( QStringLiteral( "single_input.tif" ) );
    sicnu::testing::RsSyntheticRasterBuilder singleBuilder( 16, 16, 1, GDT_Float32 );
    singleBuilder.withConstantValue( 1, 1.0f );
    singleBuilder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( singleInput );
    RSOperatorContext maskContext;
    auto runMask = [ & ]( const std::string &threshold, const QString &out ) {
      registerEnsemble( "der-mask-" + threshold, threshold );
      ModelExecutionRequest request;
      request.inputPath = singleInput.toStdString();
      request.outputPath = out.toStdString();
      request.modelReference = "der-mask-" + threshold;
      request.outputMode = RasterOutputMode::Mask;
      REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( request, maskContext ) );
    };
    // 1.25 >= 1.0 → 1; 1.25 < 1.5 → 0.
    runMask( "1.0", dir.filePath( QStringLiteral( "mask_low.tif" ) ) );
    runMask( "1.5", dir.filePath( QStringLiteral( "mask_high.tif" ) ) );
    CHECK( readPixel( dir.filePath( QStringLiteral( "mask_low.tif" ) ), 1, 4, 4 ) == 1.0f );
    CHECK( readPixel( dir.filePath( QStringLiteral( "mask_high.tif" ) ), 1, 4, 4 ) == 0.0f );
  }

  SECTION( "vote + mask/confidence is a typed refusal" )
  {
    Json::Value ensemble( Json::objectValue );
    ensemble["name"] = "der-vote";
    ensemble["task"] = "segmentation";
    ensemble["framework"] = "onnx";
    Json::Value members( Json::arrayValue );
    members.append( "der-a" );
    members.append( "der-b" );
    ensemble["ensemble"]["members"] = members;
    ensemble["ensemble"]["combination"] = "weighted_vote";
    ensemble["ensemble"]["uncertainty"] = "agreement";
    registerManifest( ensemble,
                      dir.filePath( QStringLiteral( "der-vote/model.json" ) ).toStdString() );
    const QString output = dir.filePath( QStringLiteral( "vote-mask.tif" ) );
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = output.toStdString();
    request.modelReference = "der-vote";
    request.outputMode = RasterOutputMode::Mask;
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "undefined over weighted votes" ) );
    CHECK_FALSE( QFile::exists( output ) );
  }

  SECTION( "derived mode with an explicit variance band is refused" )
  {
    Json::Value ensemble( Json::objectValue );
    ensemble["name"] = "der-var";
    ensemble["task"] = "segmentation";
    ensemble["framework"] = "onnx";
    Json::Value members( Json::arrayValue );
    members.append( "der-a" );
    members.append( "der-b" );
    ensemble["ensemble"]["members"] = members;
    ensemble["ensemble"]["uncertainty"] = "variance";
    registerManifest( ensemble,
                      dir.filePath( QStringLiteral( "der-var/model.json" ) ).toStdString() );
    const QString output = dir.filePath( QStringLiteral( "var-labels.tif" ) );
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = output.toStdString();
    request.modelReference = "der-var";
    request.outputMode = RasterOutputMode::Labels;
    REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                         Catch::Matchers::ContainsSubstring( "variance uncertainty band" ) );
    CHECK_FALSE( QFile::exists( output ) );
  }
}

TEST_CASE( "staging compression is byte-evidenced and value-preserving",
           "[models][ensemble][staging]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "stgfw-a", 2.0 );
  const ScaleProviderGuard guardB( "stgfw-b", 0.5 );
  QTemporaryDir dir;
  registerManifest( rasterMemberManifest( "stg-a", "stgfw-a" ),
                    dir.filePath( QStringLiteral( "stg-a/model.json" ) ).toStdString() );
  registerManifest( rasterMemberManifest( "stg-b", "stgfw-b" ),
                    dir.filePath( QStringLiteral( "stg-b/model.json" ) ).toStdString() );

  auto registerEnsemble = [ & ]( const std::string &name, const std::string &compression ) {
    Json::Value ensemble( Json::objectValue );
    ensemble["name"] = name;
    ensemble["task"] = "segmentation";
    ensemble["framework"] = "onnx";
    Json::Value members( Json::arrayValue );
    members.append( "stg-a" );
    members.append( "stg-b" );
    ensemble["ensemble"]["members"] = members;
    ensemble["ensemble"]["uncertainty"] = "none";
    if ( !compression.empty() )
      ensemble["ensemble"]["staging_compression"] = compression;
    registerManifest( ensemble, dir.filePath( QString::fromStdString( name ) + "/model.json" )
                                  .toStdString() );
  };
  registerEnsemble( "stg-deflate", std::string() );      // default = deflate
  registerEnsemble( "stg-none", "none" );

  // A compressible ramp: the combine stage content is deterministic, so the
  // byte comparison is machine-independent (no wall-clock involved).
  sicnu::testing::RsSyntheticRasterBuilder builder( 64, 64, 4, GDT_Float32 );
  for ( int b = 1; b <= 4; ++b )
    builder.withRampPattern( b, 0.0f, 1.0f );
  const QString input = dir.filePath( QStringLiteral( "stg_input.tif" ) );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( input );
  RSOperatorContext context;

  auto run = [ & ]( const std::string &model, const QString &out ) {
    ModelExecutionRequest request;
    request.inputPath = input.toStdString();
    request.outputPath = out.toStdString();
    request.modelReference = model;
    return sicnu::operators::runtime::runModelInference( request, context );
  };

  const QString deflateOut = dir.filePath( QStringLiteral( "stg_deflate.tif" ) );
  const QString noneOut = dir.filePath( QStringLiteral( "stg_none.tif" ) );
  REQUIRE_NOTHROW( run( "stg-deflate", deflateOut ) );
  REQUIRE_NOTHROW( run( "stg-none", noneOut ) );

  const QFileInfo deflateInfo( deflateOut );
  const QFileInfo noneInfo( noneOut );
  REQUIRE( deflateInfo.exists() );
  REQUIRE( noneInfo.exists() );
  // Structural evidence: the compressed stage is materially smaller.
  CHECK( deflateInfo.size() < noneInfo.size() );

  // Value-preserving: every pixel identical between the two products.
  constexpr int kBands = 4;
  constexpr int kSize = 64;
  for ( int b = 1; b <= kBands; ++b )
  {
    for ( int y = 0; y < kSize; y += 7 )
    {
      for ( int x = 0; x < kSize; x += 7 )
      {
        const float deflateValue = readPixel( deflateOut, b, x, y );
        const float noneValue = readPixel( noneOut, b, x, y );
        CHECK( deflateValue == noneValue );
      }
    }
  }
  // Spot-check the combined mean: members scale the ramp ×2 and ×0.5. The
  // builder's ramp value is the FLAT band index / (pixels - 1), so pixel
  // (3,0) of a 64×64 band carries 3/4095.
  const float ramp = 3.0f / 4095.0f;
  CHECK( readPixel( deflateOut, 1, 3, 0 ) == Catch::Approx( ( ramp * 2.0f + ramp * 0.5f ) / 2.0f )
                                              .margin( 1e-6f ) );

  // Zero residue after both runs.
  const QStringList residue =
    QDir( dir.path() ).entryList( { "*.tmp~*", ".*.tmp~*", "*.prev~*" },
                                  QDir::Files | QDir::Hidden );
  CHECK( residue.isEmpty() );
}

TEST_CASE( "ensemble manifest vocabulary covers the 13.0 contract",
           "[models][ensemble][manifest]" )
{
  RegistryReset reset;
  auto issuesFor = []( const Json::Value &json ) {
    return ModelCatalog::instance().validateManifestJson(
      Json::writeString( Json::StreamWriterBuilder(), json ) );
  };
  auto baseEnsemble = []() {
    Json::Value json( Json::objectValue );
    json["name"] = "ens";
    json["task"] = "segmentation";
    json["framework"] = "onnx";
    json["ensemble"]["members"].append( "m1" );
    json["ensemble"]["members"].append( "m2" );
    return json;
  };

  SECTION( "wbf combination, detection knobs, budget and staging are legal" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["combination"] = "wbf";
    json["ensemble"]["detection"]["iou_threshold"] = 0.6;
    json["ensemble"]["detection"]["skip_box_threshold"] = 0.05;
    json["ensemble"]["max_concurrent_members"] = 3;
    json["ensemble"]["staging_compression"] = "deflate";
    CHECK( issuesFor( json ).empty() );
  }

  SECTION( "detection knobs on a non-wbf combination are refused" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["detection"]["iou_threshold"] = 0.6;
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "'wbf' only" ) != std::string::npos );
  }

  SECTION( "an out-of-range fusion threshold is refused" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["combination"] = "wbf";
    json["ensemble"]["detection"]["iou_threshold"] = 1.5;
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "iou_threshold" ) != std::string::npos );
  }

  SECTION( "an out-of-range member budget is refused" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["max_concurrent_members"] = 0;
    CHECK( issuesFor( json ).empty() ); // 0 = auto is legal
    json["ensemble"]["max_concurrent_members"] = 17;
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "max_concurrent_members" ) != std::string::npos );
  }

  SECTION( "an unknown staging compression token is refused" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["staging_compression"] = "lzma";
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "staging_compression" ) != std::string::npos );
  }

  SECTION( "a non-integral member budget is refused" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["max_concurrent_members"] = 3.7;
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "whole number" ) != std::string::npos );
  }

  SECTION( "a non-string staging compression is refused" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["staging_compression"] = 5;
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "staging_compression must be a string" ) != std::string::npos );
  }

  SECTION( "a non-object detection fusion block is refused" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["detection"] = "wbf";
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "ensemble.detection must be an object" ) != std::string::npos );
  }

  SECTION( "an unknown ensemble key is still refused (closed vocabulary)" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["stratgey"] = "typo";
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "ensemble." ) != std::string::npos );
  }

  SECTION( "an uncertainty token contradicting wbf is refused" )
  {
    Json::Value json = baseEnsemble();
    json["ensemble"]["combination"] = "wbf";
    json["ensemble"]["uncertainty"] = "variance";
    const std::vector<std::string> issues = issuesFor( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "contradicts" ) != std::string::npos );
  }
}

// ---------------------------------------------------------------------------
// Hardening 15/20: non-finite member scores must fail CLOSED. std::clamp
// passes NaN through, a single NaN logit poisons the whole softmax, and the
// published artifact used to carry NaN probabilities verbatim (or elect
// class 0 under weighted voting, since NaN never wins a `>` comparison).
// ---------------------------------------------------------------------------

TEST_CASE( "non-finite member scores fail the scene ensemble typed (hardening 15/20)",
           "[models][ensemble][scene][p15]" )
{
  RegistryReset reset;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const SceneProviderGuard guardBad( "scfw-nan", { nan, 1.0 } );
  const SceneProviderGuard guardGood( "scfw-ok", { 0.5, 2.5 } );
  QTemporaryDir dir;
  registerManifest( sceneMemberManifest( "scene-nan", "scfw-nan", { "water", "land" }, "logit" ),
                    dir.filePath( QStringLiteral( "scene-nan/model.json" ) ).toStdString() );
  registerManifest( sceneMemberManifest( "scene-ok", "scfw-ok", { "water", "land" }, "logit" ),
                    dir.filePath( QStringLiteral( "scene-ok/model.json" ) ).toStdString() );

  Json::Value ensemble( Json::objectValue );
  ensemble["name"] = "scene-nan-ens";
  ensemble["task"] = "classification";
  ensemble["framework"] = "onnx";
  Json::Value members( Json::arrayValue );
  Json::Value a( Json::objectValue );
  a["model"] = "scene-nan";
  a["weight"] = 1.0;
  members.append( a );
  Json::Value b( Json::objectValue );
  b["model"] = "scene-ok";
  b["weight"] = 1.0;
  members.append( b );
  ensemble["ensemble"]["members"] = members;
  registerManifest( ensemble,
                    dir.filePath( QStringLiteral( "scene-nan-ens/model.json" ) ).toStdString() );

  const QString input = writeConstantRaster( dir, "scene_nan_input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "scene-nan.json" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "scene-nan-ens";
  request.asSceneClassification = true;
  RSOperatorContext context;
  // The NaN member is a typed member failure (surfaced with the lowest-index
  // failure semantics), never a published NaN artifact.
  REQUIRE_THROWS_AS( sicnu::operators::runtime::runModelInference( request, context ),
                     RSOperatorError );
  CHECK_FALSE( QFile::exists( output ) );
}

TEST_CASE( "single-model scene classification refuses non-finite scores (hardening 15/20)",
           "[models][ensemble][scene][p15]" )
{
  RegistryReset reset;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  // "probability" semantics would previously clamp NaN straight into the
  // published probabilities (std::clamp(NaN) == NaN).
  const SceneProviderGuard guard( "scfw-nan-single", { 2.0, nan } );
  QTemporaryDir dir;
  registerManifest(
    sceneMemberManifest( "scene-nan-single", "scfw-nan-single", { "water", "land" }, "probability" ),
    dir.filePath( QStringLiteral( "scene-nan-single/model.json" ) ).toStdString() );

  const QString input = writeConstantRaster( dir, "scene_single_input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "scene-nan-single.json" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "scene-nan-single";
  request.asSceneClassification = true;
  RSOperatorContext context;
  REQUIRE_THROWS_AS( sicnu::operators::runtime::runModelInference( request, context ),
                     RSOperatorError );
  CHECK_FALSE( QFile::exists( output ) );
}
