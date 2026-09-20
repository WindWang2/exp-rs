// tests/test_tile_semantic_equivalence.cpp — Oracle suite for the tile
// inference geometry: a fixed small model (the committed identity ONNX
// fixture, position-independent by construction) produces BYTE-IDENTICAL
// probability stacks under different batch/tile/overlap splits; extreme
// manifest integers are refused or safely bounded instead of overflowing;
// and a terminal OOM leaves no output, no sidecar and no staging residue.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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
using sicnu::operators::runtime::TtaMode;
using sicnu::operators::runtime::TileInferenceRunOptions;
using sicnu::operators::runtime::TileInferenceStats;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

class IdentityRuntime final : public IModelRuntime
{
  public:
    explicit IdentityRuntime( std::string artifact ) : m_artifact( std::move( artifact ) ) {}
    std::string framework() const override { return "eqfw"; }
    std::string backendName() const override { return "identity"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }
    cv::Mat infer( const cv::Mat &blob ) override { return blob.clone(); }

  private:
    std::string m_artifact;
};

struct RegistryReset
{
    RegistryReset()
    {
      auto &registry = ModelRuntimeRegistry::instance();
      registry.releaseAll();
      registry.resetLoadCount();
      registry.setMaxCachedSessions( 4 );
      registry.setIdleEvictionMs( 0 );
    }
    ~RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
};

struct EquivalenceGuard
{
    EquivalenceGuard()
    {
      ModelRuntimeRegistry::instance().registerProvider(
        "eqfw",
        [ ]( const ModelInfo &model, const ModelHardwareCapabilities &, std::string *error ) -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.empty() )
          {
            if ( error )
              *error = "no artifact";
            return nullptr;
          }
          return std::make_shared<IdentityRuntime>( model.resolvedArtifactPath );
        } );
    }
};

/// Registers an identity-model manifest with the given tiling knobs.
std::string installIdentityModel( const QTemporaryDir &dir, const std::string &name,
                                  int tileSize, int batchSize, int overlap,
                                  const std::string &extraRuntime = std::string() )
{
  const std::string artifact = identityModelPath().toStdString();
  std::string runtimeJson = extraRuntime.empty()
                              ? std::string()
                              : ", " + extraRuntime;
  const std::string json = R"json({
    "name": ")json" + name + R"json(",
    "task": "segmentation",
    "framework": "eqfw",
    "artifact": { "path": ")json" + artifact + R"json(" },
    "tiling": { "tile_size": )json" + std::to_string( tileSize ) + R"json(, "overlap": )json"
                + std::to_string( overlap ) + R"json(, "batch_size": )json" + std::to_string( batchSize )
                + R"json( },
    "output": { "classes": ["a", "b"] }
    )json" + runtimeJson + R"json(
  })json";
  std::string error;
  if ( !ModelCatalog::instance().registerManifestJson(
         json, dir.filePath( QString::fromStdString( name ) + QStringLiteral( "/model.json" ) )
                 .toStdString(),
         &error ) )
    return error;
  return {};
}

/// A deterministic two-band ramp raster (values depend on position, so any
/// stitch seam error is observable).
QString writeRampRaster( const QTemporaryDir &dir, const QString &name, int size )
{
  const QString path = dir.filePath( name );
  sicnu::testing::RsSyntheticRasterBuilder builder( size, size, 2, GDT_Float32 );
  for ( int b = 1; b <= 2; ++b )
    builder.withRampPattern( b, static_cast<float>( b ), 10.0f * static_cast<float>( b ) );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( path );
  return path;
}

/// Full-product byte comparison of two float32 rasters.
bool rastersByteIdentical( const QString &pathA, const QString &pathB, std::string *why )
{
  GDALDataset *a = GDALDataset::Open( pathA.toUtf8().constData(), GA_ReadOnly );
  GDALDataset *b = GDALDataset::Open( pathB.toUtf8().constData(), GA_ReadOnly );
  if ( !a || !b )
  {
    if ( why )
      *why = "open failure";
    return false;
  }
  const bool sameShape = GDALGetRasterXSize( GDALDataset::ToHandle( a ) )
                             == GDALGetRasterXSize( GDALDataset::ToHandle( b ) )
                           && GDALGetRasterYSize( GDALDataset::ToHandle( a ) )
                             == GDALGetRasterYSize( GDALDataset::ToHandle( b ) )
                           && a->GetRasterCount() == b->GetRasterCount();
  if ( !sameShape )
  {
    if ( why )
      *why = "shape mismatch";
    GDALClose( a );
    GDALClose( b );
    return false;
  }
  bool identical = true;
  const int width = GDALGetRasterXSize( GDALDataset::ToHandle( a ) );
  const int height = GDALGetRasterYSize( GDALDataset::ToHandle( a ) );
  std::vector<float> bufferA( static_cast<std::size_t>( width ), 0.0f );
  std::vector<float> bufferB( static_cast<std::size_t>( width ), 0.0f );
  for ( int band = 1; band <= a->GetRasterCount() && identical; ++band )
  {
    for ( int y = 0; y < height && identical; ++y )
    {
      a->GetRasterBand( band )->RasterIO( GF_Read, 0, y, width, 1, bufferA.data(), width, 1,
                                          GDT_Float32, 0, 0 );
      b->GetRasterBand( band )->RasterIO( GF_Read, 0, y, width, 1, bufferB.data(), width, 1,
                                          GDT_Float32, 0, 0 );
      for ( int x = 0; x < width; ++x )
      {
        const float va = bufferA[static_cast<std::size_t>( x )];
        const float vb = bufferB[static_cast<std::size_t>( x )];
        const bool bothNaN = std::isnan( va ) && std::isnan( vb );
        if ( !bothNaN && std::memcmp( &va, &vb, sizeof( float ) ) != 0 )
        {
          if ( why )
            *why = "band " + std::to_string( band ) + " row " + std::to_string( y ) + " col "
                     + std::to_string( x );
          identical = false;
          break;
        }
      }
    }
  }
  GDALClose( a );
  GDALClose( b );
  return identical;
}

ModelExecutionRequest runRequest( const std::string &input, const std::string &output,
                                  const std::string &model )
{
  ModelExecutionRequest request;
  request.inputPath = input;
  request.outputPath = output;
  request.modelReference = model;
  return request;
}

} // namespace

TEST_CASE( "identity model is semantically equivalent across batch and tile splits",
           "[models][equivalence]" )
{
  RegistryReset reset;
  const EquivalenceGuard guard;
  QTemporaryDir dir;

  // One input; one reference run; variants differ in tile/batch/overlap.
  const QString input = writeRampRaster( dir, "input.tif", 64 );
  const QString reference = dir.filePath( QStringLiteral( "ref.tif" ) );

  REQUIRE( installIdentityModel( dir, "eq-ref", 32, 1, 16 ).empty() );
  RSOperatorContext contextRef;
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference(
    runRequest( input.toStdString(), reference.toStdString(), "eq-ref" ), contextRef ) );
  REQUIRE( QFile::exists( reference ) );

  struct Variant
  {
    const char *name;
    int tileSize;
    int batch;
    int overlap;
  };
  const std::vector<Variant> variants = {
    { "eq-b4", 32, 4, 16 },   // batch only
    { "eq-t64", 64, 1, 16 },  // single tile covers the raster
    { "eq-t16", 16, 2, 8 },   // more, smaller tiles
    { "eq-novl", 32, 2, 0 },  // no overlap at all
  };

  for ( const Variant &variant : variants )
  {
    INFO( variant.name );
    REQUIRE( installIdentityModel( dir, variant.name, variant.tileSize, variant.batch,
                                   variant.overlap ).empty() );
    const QString output = dir.filePath( QStringLiteral( "%1.tif" ).arg( variant.name ) );
    RSOperatorContext context;
    REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference(
      runRequest( input.toStdString(), output.toStdString(), variant.name ), context ) );
    REQUIRE( QFile::exists( output ) );
    std::string why;
    REQUIRE( rastersByteIdentical( reference, output, &why ) );
  }
}

TEST_CASE( "extreme manifest integers are bounded or refused — never overflow",
           "[models][extremes]" )
{
  RegistryReset reset;

  SECTION( "absurd tile sizes" )
  {
    const std::string json = R"json({
      "name": "x-tile",
      "task": "segmentation",
      "framework": "eqfw",
      "artifact": { "path": "w.onnx" },
      "tiling": { "tile_size": 2147483647, "halo": 1073741824 }
    })json";
    const std::vector<std::string> issues = ModelCatalog::instance().validateManifestJson( json );
    REQUIRE_FALSE( issues.empty() );
    CHECK( issues.front().find( "absurdly large" ) != std::string::npos );
  }

  SECTION( "temporal axis at INT_MAX is refused, not multiplied" )
  {
    const std::string json = R"json({
      "name": "x-temporal",
      "task": "segmentation",
      "framework": "eqfw",
      "artifact": { "path": "w.onnx" },
      "inputs": [ { "name": "raster", "data_type": "raster",
                    "temporal_length": 2147483647 } ]
    })json";
    const std::vector<std::string> issues = ModelCatalog::instance().validateManifestJson( json );
    REQUIRE_FALSE( issues.empty() );
  }

  SECTION( "ensemble member explosion is bounded" )
  {
    std::string members;
    for ( int i = 0; i < 17; ++i )
    {
      if ( i )
        members += ", ";
      members += "\"m" + std::to_string( i ) + "\"";
    }
    const std::string json = R"json({
      "name": "x-ensemble",
      "task": "segmentation",
      "framework": "eqfw",
      "ensemble": { "members": [)json" + members + R"json(] }
    })json";
    const std::vector<std::string> issues = ModelCatalog::instance().validateManifestJson( json );
    REQUIRE_FALSE( issues.empty() );
  }

  SECTION( "fallback chain shape rules" )
  {
    const std::string selfRef = R"json({
      "name": "x-fb-self",
      "task": "segmentation",
      "framework": "eqfw",
      "artifact": { "path": "w.onnx" },
      "runtime": { "framework_fallback": ["eqfw", "other"] }
    })json";
    const std::vector<std::string> issuesSelf = ModelCatalog::instance().validateManifestJson( selfRef );
    REQUIRE_FALSE( issuesSelf.empty() );
    CHECK( issuesSelf.front().find( "must not repeat the primary" ) != std::string::npos );

    const std::string dup = R"json({
      "name": "x-fb-dup",
      "task": "segmentation",
      "framework": "eqfw",
      "artifact": { "path": "w.onnx" },
      "runtime": { "framework_fallback": ["a", "a"] }
    })json";
    const std::vector<std::string> issuesDup = ModelCatalog::instance().validateManifestJson( dup );
    REQUIRE_FALSE( issuesDup.empty() );
    CHECK( issuesDup.front().find( "twice" ) != std::string::npos );

    std::string chain;
    for ( int i = 0; i < 5; ++i )
    {
      if ( i )
        chain += ", ";
      chain += "\"f" + std::to_string( i ) + "\"";
    }
    const std::string tooLong = R"json({
      "name": "x-fb-long",
      "task": "segmentation",
      "framework": "eqfw",
      "artifact": { "path": "w.onnx" },
      "runtime": { "framework_fallback": [)json" + chain + R"json(] }
    })json";
    const std::vector<std::string> issuesLong = ModelCatalog::instance().validateManifestJson( tooLong );
    REQUIRE_FALSE( issuesLong.empty() );
    CHECK( issuesLong.front().find( "maximum chain length" ) != std::string::npos );
  }

  SECTION( "negative batch/tile values cannot produce negative windows" )
  {
    QTemporaryDir dir;
    const EquivalenceGuard guard;
    // batch_size clamps to [1, 64] at parse; a negative tile falls to the
    // engine floor. Neither may crash or misaddress — the run stays bounded.
    REQUIRE( installIdentityModel( dir, "x-neg", -64, -5, 0 ).empty() );
    const auto model = ModelCatalog::instance().find( "x-neg" );
    REQUIRE( model.has_value() );
    CHECK( model->readiness == ModelReadiness::Ready );
    CHECK( model->tiling.batchSize >= 1 );
  }
}

TEST_CASE( "terminal OOM leaves no output, no sidecar, no residue",
           "[models][oom]" )
{
  RegistryReset reset;
  QTemporaryDir dir;

  struct OomRuntime final : IModelRuntime
  {
    std::string framework() const override { return "oomfw"; }
    std::string backendName() const override { return "oom"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return {}; }
    cv::Mat infer( const cv::Mat & ) override
    {
      throw std::runtime_error( "CUDA_ERROR_OUT_OF_MEMORY: allocation failed" );
    }
  };
  ModelRuntimeRegistry::instance().registerProvider(
    "oomfw",
    [ ]( const ModelInfo &, const ModelHardwareCapabilities &, std::string * ) -> ModelRuntimePtr {
      return std::make_shared<OomRuntime>();
    } );

  const std::string artifact = identityModelPath().toStdString();
  const std::string json = R"json({
    "name": "oom-model",
    "task": "segmentation",
    "framework": "oomfw",
    "artifact": { "path": ")json" + artifact + R"json(" },
    "tiling": { "tile_size": 16, "overlap": 8, "batch_size": 1 }
  })json";
  std::string error;
  REQUIRE( ModelCatalog::instance().registerManifestJson(
    json, dir.filePath( QStringLiteral( "oom/model.json" ) ).toStdString(), &error ) );

  const QString input = dir.filePath( QStringLiteral( "input.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 48, 48, 2, GDT_Float32 )
    .withConstantValue( 1, 1.0f )
    .withConstantValue( 2, 2.0f )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "never.tif" ) );

  ModelExecutionRequest request = runRequest( input.toStdString(), output.toStdString(), "oom-model" );
  RSOperatorContext context;
  REQUIRE_THROWS_AS( sicnu::operators::runtime::runModelInference( request, context ),
                     RSOperatorError );

  // Atomic publication: nothing at the caller's path, no sidecar, no staging.
  CHECK_FALSE( QFile::exists( output ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".prov.json" ) ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".tmp~" ) ) );
}
