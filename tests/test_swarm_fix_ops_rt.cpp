// tests/test_swarm_fix_ops_rt.cpp — regression pins for the 2026-11-04 swarm
// review findings owned by the operators/runtime surfaces (OPS-RT-1..6):
//
//   OPS-RT-1  an ensemble member declaring its own derived output contract
//             (output.format / output.uncertainty / several output.heads) is a
//             typed refusal BEFORE any member session is acquired — it used to
//             write non-probability planes into the probability combine
//             (label codes averaged as probabilities, all-zero votes with
//             agreement 1.0) and publish the garbage product atomically.
//   OPS-RT-2  a second acquire of an already-loaded GPU model is a cache hit:
//             the session's own VRAM reservation no longer starves the fit
//             verdict (used to demote `auto` to a duplicate CPU session or
//             refuse an explicit cuda:N outright).
//   OPS-RT-3  OTB output cleanup finds the product behind ANY "*.out"
//             parameter, not only the literal "-out" (Segmentation's
//             -mode.vector.out / TrainImagesClassifier's -io.out used to be
//             left behind as truncated products on cancel/failure).
//   OPS-RT-4  the raster ensemble lane refuses a member class-vocabulary
//             disagreement like the scene/detection lanes do (it used to
//             silently degrade to no class metadata and mix label domains).
//   OPS-RT-5  ModelCatalog::resolve() loads the scan list on a cold catalog
//             (the only query entry that skipped ensureLoadedLocked, so plan
//             validation refused installed models in a fresh process).
//   OPS-RT-6  params::getUInt64 type-checks instead of letting jsoncpp's
//             asUInt64() throw a raw LogicError on string/negative/real input.
//
// Everything runs on GPU-less / OTB-less hosts through the injectable seams
// (counting fake providers, hardware override, a fake otbcli script).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/device_planner.h"
#include "operators/runtime/model_ensemble.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/otb/otb_operator_base.h"
#include "operators/otb/otb_segmentation_operator.h"
#include "processing/tools/tool_path_manager.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace {

using sicnu::operators::ErrorCode;
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
using sicnu::operators::runtime::ProviderTraits;
using sicnu::operators::runtime::RasterOutputMode;
using sicnu::operators::runtime::RequestedDevice;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

/// Forward-counting identity provider: members would succeed if the run were
/// allowed to reach them, so "zero forwards" pins a PRE-member refusal.
class CountingRuntime final : public IModelRuntime
{
  public:
    CountingRuntime( std::string artifact, std::string framework,
                     std::shared_ptr<std::atomic<int>> forwards )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_forwards( std::move( forwards ) ) {}
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

/// Scale provider: NCHW in → NCHW out × scale, so member products are exact,
/// hand-checkable multiples of the constant input (the positive controls).
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

/// GPU-shaped fake session for the OPS-RT-2 registry test: the device name
/// is what the provider binds to (the registry resolves the device itself).
class Sw2Session final : public IModelRuntime
{
  public:
    explicit Sw2Session( std::string device ) : m_device( std::move( device ) ) {}
    std::string framework() const override { return "sw2fw"; }
    std::string backendName() const override { return "sw2-fake"; }
    std::string deviceName() const override { return m_device; }
    std::string artifactPath() const override { return m_path; }
    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }
    std::string m_device;
    std::string m_path;
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

/// One ensemble member manifest over the committed identity fixture. @p output
/// carries the optional derived-output contract the member declares.
Json::Value swarmMemberManifest( const std::string &name, const std::string &framework,
                                 const Json::Value &output = Json::Value( Json::nullValue ),
                                 const std::vector<std::string> &classes = {} )
{
  Json::Value json( Json::objectValue );
  json[ "name" ] = name;
  json[ "task" ] = "segmentation";
  json[ "framework" ] = framework;
  json[ "artifact" ][ "path" ] = identityModelPath().toStdString();
  if ( !output.isNull() )
    json[ "output" ] = output;
  if ( !classes.empty() )
  {
    Json::Value classesJson( Json::arrayValue );
    for ( const std::string &cls : classes )
      classesJson.append( cls );
    json[ "output" ][ "classes" ] = classesJson;
  }
  return json;
}

/// Registers one manifest; fails the test with the rejection reason.
void registerSwarmManifest( const Json::Value &json, const QTemporaryDir &dir,
                            const std::string &folder )
{
  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ),
    dir.filePath( QString::fromStdString( folder ) + QStringLiteral( "/model.json" ) ).toStdString(),
    &error );
  if ( !ok )
    FAIL( "manifest '" + json[ "name" ].asString() + "' rejected: " + error );
}

/// Two-member weighted_mean ensemble over @p members (weights 1:1).
void registerSwarmEnsemble( const QTemporaryDir &dir, const std::string &ensembleName,
                            const std::string &memberA, const std::string &memberB,
                            const std::string &uncertainty = std::string( "none" ) )
{
  Json::Value json( Json::objectValue );
  json[ "name" ] = ensembleName;
  json[ "task" ] = "segmentation";
  json[ "framework" ] = "onnx";
  Json::Value &ensemble = json[ "ensemble" ] = Json::Value( Json::objectValue );
  Json::Value members( Json::arrayValue );
  Json::Value a( Json::objectValue );
  a[ "model" ] = memberA;
  a[ "weight" ] = 1.0;
  members.append( a );
  Json::Value b( Json::objectValue );
  b[ "model" ] = memberB;
  b[ "weight" ] = 1.0;
  members.append( b );
  ensemble[ "members" ] = members;
  if ( !uncertainty.empty() )
    ensemble[ "uncertainty" ] = uncertainty;
  registerSwarmManifest( json, dir, ensembleName );
}

QString writeConstantRaster( const QTemporaryDir &dir, const QString &name, int size, int bands,
                             float value )
{
  const QString path = dir.filePath( name );
  sicnu::testing::RsSyntheticRasterBuilder builder( size, size, bands, GDT_Float32 );
  for ( int b = 1; b <= bands; ++b )
    builder.withConstantValue( b, value );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( path );
  return path;
}

ModelExecutionRequest swarmRasterRequest( const std::string &input, const std::string &output,
                                          const std::string &modelRef )
{
  ModelExecutionRequest request;
  request.inputPath = input;
  request.outputPath = output;
  request.modelReference = modelRef;
  return request;
}

} // namespace

// ---------------------------------------------------------------------------
// OPS-RT-1: member derived-output contracts are a static typed refusal.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm OPS-RT-1: an ensemble member declaring a derived output contract "
           "is refused before any member forward",
           "[models][ensemble][swarm][ops-rt-1]" )
{
  RegistryReset reset;

  SECTION( "output.format=labels is refused — the combine would average label codes" )
  {
    const CountingProviderGuard guardA( "sw1fw-a" );
    const CountingProviderGuard guardB( "sw1fw-b" );
    QTemporaryDir dir;
    Json::Value labels( Json::objectValue );
    labels[ "format" ] = "labels";
    registerSwarmManifest( swarmMemberManifest( "sw1-a", "sw1fw-a", labels ), dir, "sw1-a" );
    registerSwarmManifest( swarmMemberManifest( "sw1-b", "sw1fw-b" ), dir, "sw1-b" );
    registerSwarmEnsemble( dir, "sw1-ens", "sw1-a", "sw1-b" );

    const QString input = writeConstantRaster( dir, QStringLiteral( "input.tif" ), 16, 2, 10.0f );
    const QString output = dir.filePath( QStringLiteral( "labels.tif" ) );
    RSOperatorContext context;
    bool refused = false;
    try
    {
      sicnu::operators::runtime::runModelInference(
        swarmRasterRequest( input.toStdString(), output.toStdString(), "sw1-ens" ), context );
      FAIL( "labels-declaring member did not refuse" );
    }
    catch ( const RSOperatorError &error )
    {
      refused = error.code() == ErrorCode::InvalidInputData
                && std::string( error.message() ).find( "output.format 'labels'" )
                     != std::string::npos;
    }
    CHECK( refused );
    // The refusal is STATIC: no member session was acquired, no forward ran,
    // and no product (garbage or otherwise) exists.
    CHECK( guardA.forwards->load() == 0 );
    CHECK( guardB.forwards->load() == 0 );
    CHECK_FALSE( QFile::exists( output ) );
  }

  SECTION( "output.uncertainty is refused — the appended band would count as a class plane" )
  {
    const CountingProviderGuard guardA( "sw1fw-c" );
    const CountingProviderGuard guardB( "sw1fw-d" );
    QTemporaryDir dir;
    Json::Value uncertainty( Json::objectValue );
    uncertainty[ "uncertainty" ] = "entropy";
    registerSwarmManifest( swarmMemberManifest( "sw1-c", "sw1fw-c", uncertainty ), dir, "sw1-c" );
    registerSwarmManifest( swarmMemberManifest( "sw1-d", "sw1fw-d" ), dir, "sw1-d" );
    registerSwarmEnsemble( dir, "sw1-ens-u", "sw1-c", "sw1-d" );

    const QString input = writeConstantRaster( dir, QStringLiteral( "input.tif" ), 16, 2, 10.0f );
    const QString output = dir.filePath( QStringLiteral( "unc.tif" ) );
    RSOperatorContext context;
    bool refused = false;
    try
    {
      sicnu::operators::runtime::runModelInference(
        swarmRasterRequest( input.toStdString(), output.toStdString(), "sw1-ens-u" ), context );
      FAIL( "uncertainty-declaring member did not refuse" );
    }
    catch ( const RSOperatorError &error )
    {
      refused = error.code() == ErrorCode::InvalidInputData
                && std::string( error.message() ).find( "output.uncertainty 'entropy'" )
                     != std::string::npos;
    }
    CHECK( refused );
    CHECK( guardA.forwards->load() == 0 );
    CHECK( guardB.forwards->load() == 0 );
    CHECK_FALSE( QFile::exists( output ) );
  }

  SECTION( "several output.heads are refused — one member contributes one probability head" )
  {
    const CountingProviderGuard guardA( "sw1fw-e" );
    const CountingProviderGuard guardB( "sw1fw-f" );
    QTemporaryDir dir;
    Json::Value heads( Json::objectValue );
    Json::Value headList( Json::arrayValue );
    Json::Value headClasses( Json::objectValue );
    headClasses[ "name" ] = "classes";
    headClasses[ "role" ] = "segmentation";
    headList.append( headClasses );
    Json::Value headEmbed( Json::objectValue );
    headEmbed[ "name" ] = "embed";
    headEmbed[ "role" ] = "embedding";
    headList.append( headEmbed );
    heads[ "heads" ] = headList;
    // Self-check the manifest is legal BEFORE asserting the ensemble refusal,
    // so the oracle cannot pass vacuously on a registration failure.
    const std::vector<std::string> issues = ModelCatalog::instance().validateManifestJson(
      Json::writeString( Json::StreamWriterBuilder(),
                         swarmMemberManifest( "sw1-e", "sw1fw-e", heads ) ) );
    REQUIRE( issues.empty() );
    registerSwarmManifest( swarmMemberManifest( "sw1-e", "sw1fw-e", heads ), dir, "sw1-e" );
    registerSwarmManifest( swarmMemberManifest( "sw1-f", "sw1fw-f" ), dir, "sw1-f" );
    registerSwarmEnsemble( dir, "sw1-ens-h", "sw1-e", "sw1-f" );

    const QString input = writeConstantRaster( dir, QStringLiteral( "input.tif" ), 16, 2, 10.0f );
    const QString output = dir.filePath( QStringLiteral( "heads.tif" ) );
    RSOperatorContext context;
    bool refused = false;
    try
    {
      sicnu::operators::runtime::runModelInference(
        swarmRasterRequest( input.toStdString(), output.toStdString(), "sw1-ens-h" ), context );
      FAIL( "multi-head member did not refuse" );
    }
    catch ( const RSOperatorError &error )
    {
      refused = error.code() == ErrorCode::InvalidInputData
                && std::string( error.message() ).find( "2 output.heads" ) != std::string::npos;
    }
    CHECK( refused );
    CHECK( guardA.forwards->load() == 0 );
    CHECK( guardB.forwards->load() == 0 );
    CHECK_FALSE( QFile::exists( output ) );
  }
}

// ---------------------------------------------------------------------------
// OPS-RT-4: the raster lane enforces the shared class vocabulary.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm OPS-RT-4: a raster ensemble refuses a member class-vocabulary "
           "disagreement and honors a shared one",
           "[models][ensemble][swarm][ops-rt-4]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "sw4fw-a", 2.0 );
  const ScaleProviderGuard guardB( "sw4fw-b", 0.5 );
  QTemporaryDir dir;
  const QString input = writeConstantRaster( dir, QStringLiteral( "input.tif" ), 16, 2, 10.0f );
  RSOperatorContext context;

  SECTION( "different vocabularies are a typed refusal before any member forward" )
  {
    const CountingProviderGuard countingA( "sw4fw-c" );
    const CountingProviderGuard countingB( "sw4fw-d" );
    registerSwarmManifest( swarmMemberManifest( "sw4-c", "sw4fw-c", Json::Value( Json::nullValue ),
                                                { "bg", "fg" } ),
                           dir, "sw4-c" );
    registerSwarmManifest( swarmMemberManifest( "sw4-d", "sw4fw-d", Json::Value( Json::nullValue ),
                                                { "water", "land" } ),
                           dir, "sw4-d" );
    registerSwarmEnsemble( dir, "sw4-ens-split", "sw4-c", "sw4-d" );

    const QString output = dir.filePath( QStringLiteral( "split.tif" ) );
    bool refused = false;
    try
    {
      sicnu::operators::runtime::runModelInference(
        swarmRasterRequest( input.toStdString(), output.toStdString(), "sw4-ens-split" ), context );
      FAIL( "vocabulary disagreement did not refuse" );
    }
    catch ( const RSOperatorError &error )
    {
      refused = error.code() == ErrorCode::InvalidInputData
                && std::string( error.message() ).find( "different class vocabulary" )
                     != std::string::npos;
    }
    CHECK( refused );
    CHECK( countingA.forwards->load() == 0 );
    CHECK( countingB.forwards->load() == 0 );
    CHECK_FALSE( QFile::exists( output ) );
  }

  SECTION( "one member declaring and the other silent is also a disagreement" )
  {
    const CountingProviderGuard countingA( "sw4fw-e" );
    const CountingProviderGuard countingB( "sw4fw-f" );
    registerSwarmManifest( swarmMemberManifest( "sw4-e", "sw4fw-e", Json::Value( Json::nullValue ),
                                                { "bg", "fg" } ),
                           dir, "sw4-e" );
    registerSwarmManifest( swarmMemberManifest( "sw4-f", "sw4fw-f" ), dir, "sw4-f" );
    registerSwarmEnsemble( dir, "sw4-ens-mixed", "sw4-e", "sw4-f" );

    const QString output = dir.filePath( QStringLiteral( "mixed.tif" ) );
    bool refused = false;
    try
    {
      sicnu::operators::runtime::runModelInference(
        swarmRasterRequest( input.toStdString(), output.toStdString(), "sw4-ens-mixed" ), context );
      FAIL( "mixed vocabulary silence did not refuse" );
    }
    catch ( const RSOperatorError &error )
    {
      refused = error.code() == ErrorCode::InvalidInputData
                && std::string( error.message() ).find( "different class vocabulary" )
                     != std::string::npos;
    }
    CHECK( refused );
    CHECK_FALSE( QFile::exists( output ) );
  }

  SECTION( "a shared vocabulary still runs and names the classes on the product" )
  {
    registerSwarmManifest( swarmMemberManifest( "sw4-a", "sw4fw-a", Json::Value( Json::nullValue ),
                                                { "bg", "fg" } ),
                           dir, "sw4-a" );
    registerSwarmManifest( swarmMemberManifest( "sw4-b", "sw4fw-b", Json::Value( Json::nullValue ),
                                                { "bg", "fg" } ),
                           dir, "sw4-b" );
    registerSwarmEnsemble( dir, "sw4-ens-same", "sw4-a", "sw4-b" );

    const QString output = dir.filePath( QStringLiteral( "same.tif" ) );
    ModelExecutionRequest request =
      swarmRasterRequest( input.toStdString(), output.toStdString(), "sw4-ens-same" );
    // Labels collapse: the label domain IS the shared vocabulary's size, so
    // the product carries the class names as metadata.
    request.outputMode = RasterOutputMode::Labels;
    ModelExecutionResult result;
    REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );
    CHECK( QFile::exists( output ) );
    CHECK( result.rasterStats.outBands == 1 );
    GDALDataset *ds = GDALDataset::Open( output.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    const char *names = GDALGetMetadataItem( ds, "SICNU_CLASS_NAMES", "" );
    CHECK( ( names != nullptr && std::string( names ) == "bg;fg" ) );
    GDALClose( ds );
  }
}

// ---------------------------------------------------------------------------
// OPS-RT-2: a loaded session's own reservation must not starve its re-acquire.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm OPS-RT-2: a second acquire of a loaded GPU model hits the cache "
           "instead of starving on its own reservation",
           "[models][runtime][swarm][ops-rt-2]" )
{
  QTemporaryDir dir;
  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  registry.vramLedger().reset();
  registry.resetLoadCount();
  registry.setMaxCachedSessions( 8 );
  registry.setIdleEvictionMs( 0 );

  ModelHardwareCapabilities hw;
  hw.cudaAvailable = true;
  hw.cudaDeviceCount = 1;
  hw.vramBudgetMb = 100; // one card, 100 MiB
  registry.setHardwareForTest( hw );
  registry.registerProvider(
    "sw2fw",
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      return std::make_shared<Sw2Session>( "cuda:0" );
    },
    ProviderTraits{ /*maxAddressableCudaIndex*/ 0 } );

  auto makeModel = [ & ]( const std::string &name, int estimatedVramMb, bool cpuFallback ) {
    ModelInfo model;
    model.name = name;
    model.id = name;
    model.framework = "sw2fw";
    model.runtime.gpu = true;
    model.runtime.estimatedVramMb = estimatedVramMb;
    model.runtime.cpuFallback = cpuFallback;
    const QString path = dir.filePath( QString::fromStdString( name + ".bin" ) );
    {
      QFile file( path );
      REQUIRE( file.open( QIODevice::WriteOnly ) );
      file.write( QByteArray( "sw2-weights-" ) + QByteArray::number( estimatedVramMb ) );
      file.close();
    }
    model.resolvedArtifactPath = path.toStdString();
    model.contentDigest = "sw2-digest-" + name;
    return model;
  };

  std::string error;
  const ModelInfo gpuModel = makeModel( "sw2-model", 60, /*cpuFallback*/ false );

  SECTION( "explicit cuda:0 re-acquire serves the live session (no refusal, no reload)" )
  {
    auto first = registry.acquire( gpuModel, RequestedDevice::cuda( 0 ), &error );
    REQUIRE( first );
    CHECK( registry.vramLedger().reservedMb( 0 ) == 60 );

    // The card reads 40 MiB free — the session's OWN 60 MiB reservation. It
    // must not compete with this acquisition of the same identity: the cache
    // lookup serves the live session. Pre-fix this was a typed refusal
    // ("device unavailable") after one pointless eviction pass.
    auto second = registry.acquire( gpuModel, RequestedDevice::cuda( 0 ), &error );
    REQUIRE( second );
    CHECK( second.get() == first.get() );
    CHECK( registry.totalSessionsLoaded() == 1 );
    CHECK( registry.vramLedger().reservedMb( 0 ) == 60 ); // one booking, not two
  }

  SECTION( "auto with CPU fallback does not demote to a duplicate CPU session" )
  {
    const ModelInfo fallbackModel = makeModel( "sw2-model-fb", 60, /*cpuFallback*/ true );
    auto first = registry.acquire( fallbackModel, RequestedDevice::autoDetect(), &error );
    REQUIRE( first );
    CHECK( first->deviceName() == "cuda:0" );

    // Pre-fix: free(0)=40 < 60 → auto demoted to cpu → cache-key mismatch →
    // a SECOND session loaded (CPU copy of live weights). The session's own
    // reservation is discounted now, so auto re-resolves cuda:0 and hits.
    auto second = registry.acquire( fallbackModel, RequestedDevice::autoDetect(), &error );
    REQUIRE( second );
    CHECK( second.get() == first.get() );
    CHECK( second->deviceName() == "cuda:0" );
    CHECK( registry.totalSessionsLoaded() == 1 );
  }

  SECTION( "a DIFFERENT identity is still refused honestly (the discount is identity-scoped)" )
  {
    auto first = registry.acquire( gpuModel, RequestedDevice::cuda( 0 ), &error );
    REQUIRE( first );
    const ModelInfo other = makeModel( "sw2-other", 60, /*cpuFallback*/ false );
    // 60 (own) + 60 (other) > 100: the other identity's reservation is real
    // competing VRAM and must stay counted.
    CHECK_FALSE( registry.acquire( other, RequestedDevice::cuda( 0 ), &error ) );
    CHECK( error.find( "device unavailable" ) != std::string::npos );
    CHECK( registry.vramLedger().reservedMb( 0 ) == 60 );
  }

  registry.releaseAll();
  registry.vramLedger().reset();
  registry.setHardwareForTest( std::nullopt );
  registry.setMaxCachedSessions( 2 );
}

// ---------------------------------------------------------------------------
// OPS-RT-3: OTB output cleanup matches every "*.out" parameter.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm OPS-RT-3: OTB output cleanup preserves the truncated product "
           "behind a dotted '.out' parameter",
           "[models][otb][swarm][ops-rt-3]" )
{
#if !defined( Q_OS_UNIX )
  SKIP( "fake otbcli script requires a POSIX shell" );
#else
  QTemporaryDir dir;

  // Fake otbcli_Segmentation: writes a partial product at whatever output
  // parameter it was given (any "-out"/"*.out" value), then fails — the
  // cancel/failure shape the cleanup exists for.
  const QString scriptPath =
    dir.filePath( QStringLiteral( "otbcli_Segmentation" ) );
  {
    QFile script( scriptPath );
    REQUIRE( script.open( QIODevice::WriteOnly | QIODevice::Text ) );
    script.write(
      "#!/bin/sh\n"
      "out=\"\"\n"
      "prev=\"\"\n"
      "for a in \"$@\"; do\n"
      "  case \"$prev\" in -out|*.out) out=\"$a\" ;; esac\n"
      "  prev=\"$a\"\n"
      "done\n"
      "printf 'PARTIAL-PRODUCT' > \"$out\"\n"
      "exit 1\n" );
    script.close();
    REQUIRE( script.setPermissions( QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                    | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                    | QFileDevice::ExeOther ) );
  }
  // Point the tool manager at the fake CLI (restored at scope exit).
  struct OtbPathReset
  {
      QString saved;
      ~OtbPathReset() { ToolPathManager::instance().setOtbPath( saved ); }
  } otbReset{ QString() };
  ToolPathManager::instance().setOtbPath( dir.path() );

  // Existing input (buildOtbArgs requires it) and the product path.
  const QString input = dir.filePath( QStringLiteral( "input.tif" ) );
  {
    QFile file( input );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QByteArray( "not-a-real-raster" ) );
    file.close();
  }
  const QString output = dir.filePath( QStringLiteral( "segmented.gpkg" ) );

  Json::Value params( Json::objectValue );
  params[ "input" ] = input.toStdString();
  params[ "output" ] = output.toStdString();
  params[ "outputMode" ] = "vector"; // → args carry "-mode.vector.out <path>"

  sicnu::operators::otb::OtbSegmentationOperator segmentationOperator;
  RSOperatorContext context;
  bool failed = false;
  try
  {
    segmentationOperator.run( params, context );
    FAIL( "the fake OTB CLI failed but run() returned" );
  }
  catch ( const RSOperatorError &error )
  {
    failed = error.code() == ErrorCode::OtbError;
  }
  CHECK( failed );

  // THE ORACLE: the truncated product was found behind "-mode.vector.out"
  // and preserved as ".partial" — never left looking like a valid product.
  // Pre-fix the literal "-out" scan matched nothing and the truncated file
  // stayed at the product path.
  CHECK( QFile::exists( output + QStringLiteral( ".partial" ) ) );
  CHECK_FALSE( QFile::exists( output ) );
#endif
}

// ---------------------------------------------------------------------------
// OPS-RT-5: resolve() loads the scan list on a cold catalog.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm OPS-RT-5: ModelCatalog::resolve finds installed models on a cold catalog",
           "[models][catalog][swarm][ops-rt-5]" )
{
  QTemporaryDir dir;
  auto &catalog = ModelCatalog::instance();

  // models/<name>/model.json layout with a readable artifact (Ready).
  const QString weights = dir.filePath( QStringLiteral( "sw5-model/weights.onnx" ) );
  REQUIRE( QDir().mkpath( QFileInfo( weights ).absolutePath() ) );
  {
    QFile file( weights );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QByteArray( "sw5-weights" ) );
    file.close();
  }
  const QString manifest = dir.filePath( QStringLiteral( "sw5-model/model.json" ) );
  {
    QFile file( manifest );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QByteArray( R"({
        "name": "sw5-model",
        "id": "sw5/reg",
        "model_version": "1.0",
        "task": "segmentation",
        "framework": "onnx",
        "artifact": { "path": "weights.onnx" }
    })" ) );
    file.close();
  }

  // setDirectory() invalidates the load: the catalog is COLD, exactly like a
  // fresh process whose first catalog touch is plan validation's resolve().
  catalog.setDirectory( dir.path().toStdString() );

  std::string error;
  const auto exact = catalog.resolve( "sw5/reg@1.0", &error );
  REQUIRE( exact.has_value() );
  CHECK( exact->name == "sw5-model" );
  CHECK( exact->readiness == sicnu::operators::ModelReadiness::Ready );

  // Bare id (sole entry) and the empty version part ("id@") both resolve —
  // the documented lexicographic-latest semantics for a shared id.
  const auto bare = catalog.resolve( "sw5/reg", &error );
  REQUIRE( bare.has_value() );
  const auto emptyVersion = catalog.resolve( "sw5/reg@", &error );
  REQUIRE( emptyVersion.has_value() );

  // A missing version stays a typed miss listing what IS available.
  const auto missing = catalog.resolve( "sw5/reg@9.9", &error );
  CHECK_FALSE( missing.has_value() );
  CHECK( error.find( "1.0" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// OPS-RT-6: getUInt64 type-checks instead of throwing jsoncpp LogicError.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm OPS-RT-6: params::getUInt64 refuses illegal values with a typed error",
           "[models][params][swarm][ops-rt-6]" )
{
  using sicnu::operators::params::getUInt64;

  Json::Value params( Json::objectValue );
  // Missing key → default (0 = "plan estimate" for io:cache_prefetch).
  CHECK( getUInt64( params, "maxBytes", 0 ) == 0 );
  CHECK( getUInt64( params, "maxBytes", 4096 ) == 4096 );

  params[ "maxBytes" ] = Json::Value( Json::UInt64( 1048576 ) );
  CHECK( getUInt64( params, "maxBytes", 0 ) == 1048576 );
  params[ "maxBytes" ] = 0; // plain int zero is a legal non-negative value
  CHECK( getUInt64( params, "maxBytes", 7 ) == 0 );

  // The pre-fix spelling threw a raw jsoncpp LogicError for all of these;
  // the shared helper refuses with a typed RSOperatorError instead.
  auto refusesTyped = [ & ]() {
    try
    {
      ( void )getUInt64( params, "maxBytes", 0 );
      return false;
    }
    catch ( const RSOperatorError &error )
    {
      return error.code() == ErrorCode::TypeMismatch;
    }
  };
  params[ "maxBytes" ] = "1024"; // a string is not an unsigned integer
  CHECK( refusesTyped() );
  params[ "maxBytes" ] = Json::Value( Json::Int64( -1 ) ); // negatives are illegal
  CHECK( refusesTyped() );
  params[ "maxBytes" ] = 1.5; // a real is not an unsigned integer
  CHECK( refusesTyped() );

  // Document the library behavior the helper protects against: asUInt64() on
  // a string value is a LogicError, not a value.
  params[ "maxBytes" ] = "1024";
  CHECK_THROWS_AS( params[ "maxBytes" ].asUInt64(), Json::LogicError );
}
