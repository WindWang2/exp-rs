// tests/test_model_runtime.cpp — model runtime registry, providers, the
// bounded tile inference engine and the rs:infer integration (manifest-driven
// contracts, session reuse, readiness gates, real resource estimates feeding
// the ExecutionPlane). Uses the committed 136-byte identity ONNX fixture and
// tiny synthetic rasters (<=256 px) only.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "processing/framework/execution_plane.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

#include <opencv2/dnn.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::TileInferenceEngine;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

/// A deterministic fake provider: NCHW in → NCHW out, values scaled.
class FakeRuntime final : public IModelRuntime
{
  public:
    FakeRuntime( std::string artifact, float scale )
        : m_artifact( std::move( artifact ) ), m_scale( scale ) {}

    std::string framework() const override { return "fakefw"; }
    std::string backendName() const override { return "fake_backend"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }

    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      ++inferCount;
      cv::Mat out = nchwBlob.clone();
      out *= m_scale;
      return out;
    }

    int inferCount = 0;

  private:
    std::string m_artifact;
    float m_scale;
    std::mutex m_mutex;
};

/// Registers a counting fake provider for "fakefw" and returns the counters.
struct FakeProviderGuard
{
    std::shared_ptr<std::atomic<int>> loadCount = std::make_shared<std::atomic<int>>( 0 );

    FakeProviderGuard()
    {
      ModelRuntimeRegistry::instance().registerProvider(
        "fakefw", [this]( const ModelInfo &model, const ModelHardwareCapabilities &, std::string *error ) -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.find( "missing" ) != std::string::npos )
          {
            if ( error )
              *error = "artifact not found";
            return nullptr;
          }
          loadCount->fetch_add( 1 );
          return std::make_shared<FakeRuntime>( model.resolvedArtifactPath, 1.0f );
        } );
    }
};

ModelInfo fakeModel( const std::string &artifact )
{
  ModelInfo info;
  info.name = "fake-model";
  info.framework = "fakefw";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = artifact;
  return info;
}

/// RAII: pin hardware capabilities for a test scope.
struct HardwarePin
{
    HardwarePin( const ModelHardwareCapabilities &caps )
    {
      ModelRuntimeRegistry::instance().setHardwareForTest( caps );
    }
    ~HardwarePin() { ModelRuntimeRegistry::instance().setHardwareForTest( std::nullopt ); }
};

/// Writes a manifest for the identity fixture into a temp models dir and
/// points the catalog singleton at it.
void installIdentityCatalog( const QTemporaryDir &dir, const QString &name,
                             const QString &extraTiling = QString() )
{
  QDir( dir.path() ).mkpath( name );
  QFile::copy( identityModelPath(), dir.filePath( name + QStringLiteral( "/model.onnx" ) ) );
  const QString tiling = extraTiling.isEmpty()
                           ? QStringLiteral( "\"tiling\": { \"tile_size\": 64, \"overlap\": 16 }" )
                           : extraTiling;
  QFile manifest( dir.filePath( name + QStringLiteral( "/model.json" ) ) );
  REQUIRE( manifest.open( QIODevice::WriteOnly ) );
  manifest.write( QString( R"({
      "name": "%1",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": "model.onnx" },
      %2
  })" )
                     .arg( name, tiling )
                     .toUtf8() );
  manifest.close();
  ModelCatalog::instance().setDirectory( dir.path().toStdString() );
}

} // namespace

// ---------------------------------------------------------------------------
// Registry & session lifecycle
// ---------------------------------------------------------------------------

TEST_CASE( "runtime registry reuses sessions per artifact", "[models][runtime]" )
{
  auto &registry = ModelRuntimeRegistry::instance();
  FakeProviderGuard guard;
  registry.releaseAll();
  registry.resetLoadCount();
  registry.setMaxCachedSessions( 2 );

  const ModelInfo a = fakeModel( "/tmp/artifact-a.onnx" );
  const ModelInfo b = fakeModel( "/tmp/artifact-b.onnx" );

  const auto first = registry.acquire( a );
  const auto second = registry.acquire( a );
  REQUIRE( first );
  REQUIRE( second );
  CHECK( first.get() == second.get() );       // same cached session
  CHECK( guard.loadCount->load() == 1 );      // loaded once

  const auto other = registry.acquire( b );
  REQUIRE( other );
  CHECK( other.get() != first.get() );
  CHECK( guard.loadCount->load() == 2 );
  CHECK( registry.cachedSessionCount() == 2 );
}

TEST_CASE( "runtime registry evicts LRU beyond the cache bound", "[models][runtime]" )
{
  auto &registry = ModelRuntimeRegistry::instance();
  FakeProviderGuard guard;
  registry.releaseAll();
  registry.resetLoadCount();
  registry.setMaxCachedSessions( 1 );

  const ModelInfo a = fakeModel( "/tmp/artifact-a.onnx" );
  const ModelInfo b = fakeModel( "/tmp/artifact-b.onnx" );

  REQUIRE( registry.acquire( a ) );
  REQUIRE( registry.acquire( b ) );           // evicts a (LRU, bound 1)
  CHECK( registry.cachedSessionCount() == 1 );
  const auto reloaded = registry.acquire( a ); // a must be re-loaded
  REQUIRE( reloaded );
  CHECK( guard.loadCount->load() == 3 );

  registry.releaseAll();
  CHECK( registry.cachedSessionCount() == 0 );
  registry.setMaxCachedSessions( 2 ); // restore the default-ish bound for later cases
}

TEST_CASE( "registry surfaces provider load failures", "[models][runtime]" )
{
  auto &registry = ModelRuntimeRegistry::instance();
  FakeProviderGuard guard;
  registry.releaseAll();

  std::string error;
  CHECK_FALSE( registry.acquire( fakeModel( "/tmp/missing-weights.onnx" ), &error ) );
  CHECK( error.find( "artifact not found" ) != std::string::npos );

  CHECK_FALSE( registry.acquire( [] {
    ModelInfo m = fakeModel( "/tmp/x" );
    m.framework = "no-such-framework";
    return m;
  }(),
                               &error ) );
  CHECK( error.find( "no runtime provider" ) != std::string::npos );
}

TEST_CASE( "runtime-layer readiness verdicts are honest", "[models][runtime]" )
{
  // Each ctest-discovered case runs in its own process: this case needs the
  // fakefw provider registered or readiness reports UnsupportedRuntime
  // before the hardware checks it is asserting.
  FakeProviderGuard fakeProviderGuard;

  ModelHardwareCapabilities noGpu;
  ModelHardwareCapabilities withGpu;
  withGpu.cudaAvailable = true;
  withGpu.vramBudgetMb = 1024;

  std::string reason;

  ModelInfo gpuOnly = fakeModel( "/tmp/x" );
  gpuOnly.runtime.gpu = true;
  gpuOnly.runtime.cpuFallback = false;
  CHECK( sicnu::operators::runtime::evaluateRuntimeReadiness( gpuOnly, noGpu, &reason )
         == ModelReadiness::IncompatibleHardware );
  CHECK( reason.find( "CUDA is unavailable" ) != std::string::npos );
  CHECK( sicnu::operators::runtime::evaluateRuntimeReadiness( gpuOnly, withGpu, &reason )
         == ModelReadiness::Ready );

  ModelInfo gpuTooBig = gpuOnly;
  gpuTooBig.runtime.estimatedVramMb = 4096;
  CHECK( sicnu::operators::runtime::evaluateRuntimeReadiness( gpuTooBig, withGpu, &reason )
         == ModelReadiness::IncompatibleHardware );
  CHECK( reason.find( "VRAM" ) != std::string::npos );

  ModelInfo gpuWithFallback = gpuOnly;
  gpuWithFallback.runtime.cpuFallback = true;
  CHECK( sicnu::operators::runtime::evaluateRuntimeReadiness( gpuWithFallback, noGpu, &reason )
         == ModelReadiness::Ready ); // CPU fallback keeps it executable

  ModelInfo unsupported = fakeModel( "/tmp/x" );
  unsupported.framework = "tensorflow-embedded";
  CHECK( sicnu::operators::runtime::evaluateRuntimeReadiness( unsupported, withGpu, &reason )
         == ModelReadiness::UnsupportedRuntime );
  CHECK( reason.find( "no runtime provider" ) != std::string::npos );
}

TEST_CASE( "hardware capability env overrides apply", "[models][runtime]" )
{
  qputenv( "SICNU_MODEL_GPU", "1" );
  CHECK( ModelHardwareCapabilities::detect().cudaAvailable );
  qputenv( "SICNU_MODEL_GPU", "0" );
  CHECK_FALSE( ModelHardwareCapabilities::detect().cudaAvailable );
  qputenv( "SICNU_MODEL_VRAM_MB", "2048" );
  CHECK( ModelHardwareCapabilities::detect().vramBudgetMb == 2048 );
  qunsetenv( "SICNU_MODEL_GPU" );
  qunsetenv( "SICNU_MODEL_VRAM_MB" );
}

// ---------------------------------------------------------------------------
// Real ONNX provider (identity fixture) + tile inference engine
// ---------------------------------------------------------------------------

TEST_CASE( "opencv dnn provider runs the identity fixture", "[models][runtime][onnx]" )
{
  REQUIRE( QFile::exists( identityModelPath() ) );

  ModelInfo info;
  info.name = "identity";
  info.framework = "onnx";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = identityModelPath().toStdString();

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  std::string error;
  const auto session = registry.acquire( info, &error );
  REQUIRE( session );
  CHECK( session->backendName() == "opencv_dnn" );
  CHECK( session->deviceName() == "cpu" ); // no CUDA in this environment

  // (1, 1, 4, 3) blob → identity preserves values.
  cv::Mat hwc( 3, 4, CV_32FC1 ); // rows=3 (H), cols=4 (W)
  float v = 1.f;
  for ( int row = 0; row < hwc.rows; ++row )
    for ( int col = 0; col < hwc.cols; ++col )
      hwc.at<float>( row, col ) = v++;
  const cv::Mat blob = cv::dnn::blobFromImage( hwc );
  const cv::Mat out = session->infer( blob );
  REQUIRE( out.dims == 4 );
  REQUIRE( out.size[1] == 1 );
  REQUIRE( out.size[2] == 3 );
  REQUIRE( out.size[3] == 4 );
  float expected = 1.f;
  for ( int row = 0; row < 3; ++row )
    for ( int col = 0; col < 4; ++col )
    {
      CHECK( out.ptr<float>( 0, 0 )[row * 4 + col] == Catch::Approx( expected++ ).margin( 1e-4 ) );
    }
}

TEST_CASE( "tile engine preserves identity across tiles with halo", "[models][runtime][engine]" )
{
  REQUIRE( QFile::exists( identityModelPath() ) );

  QTemporaryDir dir;
  const QString inputPath = dir.filePath( QStringLiteral( "input.tif" ) );
  const QString outputPath = dir.filePath( QStringLiteral( "output.tif" ) );

  sicnu::testing::RsSyntheticRasterBuilder builder( 128, 128, 1 );
  builder.withCrs( QStringLiteral( "EPSG:32648" ) )
      .withGeoTransform( 100.0, 0.5, 200.0, -0.5 )
      .withRampPattern( 1, 0.0f, 255.0f );
  builder.writeToDisk( inputPath );

  ModelInfo info;
  info.name = "identity";
  info.framework = "onnx";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = identityModelPath().toStdString();
  info.tiling.tileSize = 64;
  info.tiling.overlap = 16; // halo 8 per side

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  const auto session = registry.acquire( info );
  REQUIRE( session );

  RSOperatorContext context;
  TileInferenceEngine engine( info, session );
  const auto stats = engine.run( inputPath.toStdString(), {}, outputPath.toStdString(), context );

  CHECK( stats.tileSize == 64 );
  CHECK( stats.halo == 8 );
  CHECK( stats.tilesProcessed == 4 ); // 2x2 core tiles
  CHECK( stats.outBands == 1 );
  CHECK( stats.outWidth == 128 );
  CHECK( stats.outHeight == 128 );

  // Identity: every output pixel equals the input ramp; halo reads must not
  // shift or duplicate anything (seam check).
  GdalDatasetWrapper out;
  REQUIRE( out.open( outputPath ) );
  REQUIRE( out.bandCount() == 1 );
  REQUIRE( out.width() == 128 );
  REQUIRE( out.height() == 128 );
  std::vector<float> pixels( 128 * 128 );
  REQUIRE( out.readBandData( 1, pixels.data(), 128, 128 ) );
  const std::vector<float> &expectedBand = builder.band( 1 );
  for ( int row = 0; row < 128; ++row )
    for ( int col = 0; col < 128; ++col )
    {
      const std::size_t i = static_cast<std::size_t>( row ) * 128 + col;
      CHECK( pixels[i] == Catch::Approx( expectedBand[i] ).margin( 1e-3 ) );
    }

  // Georeference preserved.
  CHECK( out.projection().contains( QStringLiteral( "32648" ) ) );
  const auto gt = out.geoTransform();
  CHECK( gt[0] == Catch::Approx( 100.0 ) );
  CHECK( gt[1] == Catch::Approx( 0.5 ) );
}

TEST_CASE( "tile engine batches tiles per the manifest contract", "[models][runtime][engine]" )
{
  REQUIRE( QFile::exists( identityModelPath() ) );

  QTemporaryDir dir;
  const QString inputPath = dir.filePath( QStringLiteral( "input.tif" ) );
  const QString outputPath = dir.filePath( QStringLiteral( "output.tif" ) );

  sicnu::testing::RsSyntheticRasterBuilder builder( 256, 256, 1 );
  builder.withCheckerboard( 1, 16, 10.0f, 240.0f );
  builder.writeToDisk( inputPath );

  ModelInfo info;
  info.name = "identity";
  info.framework = "onnx";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = identityModelPath().toStdString();
  info.tiling.tileSize = 64;
  info.tiling.batchSize = 4;

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  const auto session = registry.acquire( info );
  REQUIRE( session );

  RSOperatorContext context;
  TileInferenceEngine engine( info, session );
  const auto stats = engine.run( inputPath.toStdString(), {}, outputPath.toStdString(), context );
  CHECK( stats.batchSize == 4 );
  CHECK( stats.tilesProcessed == 16 );

  GdalDatasetWrapper out;
  REQUIRE( out.open( outputPath ) );
  std::vector<float> pixels( 256 * 256 );
  REQUIRE( out.readBandData( 1, pixels.data(), 256, 256 ) );
  const std::vector<float> &expectedBand = builder.band( 1 );
  for ( std::size_t i = 0; i < pixels.size(); ++i )
    CHECK( pixels[i] == Catch::Approx( expectedBand[i] ).margin( 1e-3 ) );
}

TEST_CASE( "tile engine propagates nodata and honours cancellation", "[models][runtime][engine]" )
{
  REQUIRE( QFile::exists( identityModelPath() ) );

  QTemporaryDir dir;
  const QString inputPath = dir.filePath( QStringLiteral( "input.tif" ) );
  const QString outputPath = dir.filePath( QStringLiteral( "output.tif" ) );

  sicnu::testing::RsSyntheticRasterBuilder builder( 128, 128, 1 );
  builder.withNoData( -9999.0 ).withRampPattern( 1, 0.0f, 100.0f );
  // One full core-tile region is nodata.
  for ( int y = 0; y < 64; ++y )
    for ( int x = 64; x < 128; ++x )
      builder.withPixel( 1, x, y, -9999.0f );
  builder.writeToDisk( inputPath );

  ModelInfo info;
  info.name = "identity";
  info.framework = "onnx";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = identityModelPath().toStdString();
  info.tiling.tileSize = 64;

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  const auto session = registry.acquire( info );
  REQUIRE( session );

  // Cancellation before the first tile aborts with Cancelled.
  {
    std::atomic<bool> cancelled{ true };
    RSOperatorContext context;
    context.setCancelFlag( &cancelled );
    TileInferenceEngine engine( info, session );
    bool threw = false;
    try
    {
      engine.run( inputPath.toStdString(), {}, dir.filePath( QStringLiteral( "cancelled.tif" ) ).toStdString(), context );
    }
    catch ( const RSOperatorError &e )
    {
      threw = true;
      CHECK( std::string( e.what() ).find( "cancel" ) != std::string::npos );
    }
    CHECK( threw );
  }

  // Nodata: all-invalid core pixels come back as NaN.
  RSOperatorContext context;
  TileInferenceEngine engine( info, session );
  REQUIRE_NOTHROW( engine.run( inputPath.toStdString(), {}, outputPath.toStdString(), context ) );
  GdalDatasetWrapper out;
  REQUIRE( out.open( outputPath ) );
  std::vector<float> pixels( 128 * 128 );
  REQUIRE( out.readBandData( 1, pixels.data(), 128, 128 ) );
  int nanCount = 0;
  int validChecked = 0;
  for ( int row = 0; row < 128; ++row )
    for ( int col = 0; col < 128; ++col )
    {
      const std::size_t i = static_cast<std::size_t>( row ) * 128 + col;
      if ( col >= 64 )
      {
        if ( std::isnan( pixels[i] ) )
          ++nanCount;
      }
      else
        ++validChecked;
    }
  CHECK( nanCount == 64 * 64 );
  CHECK( validChecked == 64 * 128 );
}

// ---------------------------------------------------------------------------
// rs:infer integration + admission estimates
// ---------------------------------------------------------------------------

TEST_CASE( "rs:infer resolves catalog names with manifest contracts", "[models][runtime][infer]" )
{
  QTemporaryDir dir;
  installIdentityCatalog( dir, QStringLiteral( "ident" ) );

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "rs:infer" );
  REQUIRE( op );

  const QString inputPath = dir.filePath( QStringLiteral( "input.tif" ) );
  const QString outputPath = dir.filePath( QStringLiteral( "output.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 100, 60, 1 );
  builder.withRampPattern( 1, 0.0f, 50.0f );
  builder.writeToDisk( inputPath );

  Json::Value params( Json::objectValue );
  params["input"] = inputPath.toStdString();
  params["model"] = "ident"; // catalog name — resolved via the manifest dir
  params["output"] = outputPath.toStdString();

  RSOperatorContext context;
  const Json::Value result = op->run( params, context );

  CHECK( result["backend"].asString() == "opencv_dnn" );
  CHECK( result["device"].asString() == "cpu" );
  CHECK( result["model"].asString() == "ident" );
  CHECK( result["tileSize"].asInt() == 64 );
  CHECK( result["tiles"].asInt() >= 2 );
  CHECK( result["width"].asInt() == 100 );
  CHECK( result["height"].asInt() == 60 );

  GdalDatasetWrapper out;
  REQUIRE( out.open( outputPath ) );
  std::vector<float> pixels( 100 * 60 );
  REQUIRE( out.readBandData( 1, pixels.data(), 100, 60 ) );
  const std::vector<float> &expectedBand = builder.band( 1 );
  for ( std::size_t i = 0; i < pixels.size(); ++i )
    CHECK( pixels[i] == Catch::Approx( expectedBand[i] ).margin( 1e-3 ) );
}

TEST_CASE( "rs:infer refuses non-ready catalog models with the readiness reason", "[models][runtime][infer]" )
{
  QTemporaryDir dir;
  QDir( dir.path() ).mkpath( QStringLiteral( "template" ) );
  QFile manifest( dir.filePath( QStringLiteral( "template/model.json" ) ) );
  REQUIRE( manifest.open( QIODevice::WriteOnly ) );
  manifest.write( R"({ "name": "template", "task": "segmentation", "path": "" })" );
  manifest.close();
  ModelCatalog::instance().setDirectory( dir.path().toStdString() );

  // Write a tiny input so the operator reaches the model-readiness gate
  // (otherwise it fails on the missing input file before consulting the catalog).
  {
    sicnu::testing::RsSyntheticRasterBuilder b( 16, 16, 1 );
    b.writeToDisk( dir.filePath( QStringLiteral( "input.tif" ) ) );
  }

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "rs:infer" );
  REQUIRE( op );

  Json::Value params( Json::objectValue );
  params["input"] = dir.filePath( QStringLiteral( "input.tif" ) ).toStdString();
  params["model"] = "template";
  params["output"] = dir.filePath( QStringLiteral( "output.tif" ) ).toStdString();

  RSOperatorContext context;
  bool threw = false;
  try
  {
    op->run( params, context );
  }
  catch ( const RSOperatorError &e )
  {
    threw = true;
    const std::string what = e.what();
    CHECK( what.find( "not ready" ) != std::string::npos );
    CHECK( what.find( "missing_artifact" ) != std::string::npos );
  }
  CHECK( threw );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "output.tif" ) ) ) );
}

TEST_CASE( "rs:infer estimates feed ExecutionPlane admission", "[models][runtime][admission]" )
{
  QTemporaryDir dir;
  installIdentityCatalog( dir, QStringLiteral( "ident" ),
                          QStringLiteral( "\"tiling\": { \"tile_size\": 64, \"overlap\": 16 }" ) );

  const QString inputPath = dir.filePath( QStringLiteral( "input.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder builder( 128, 128, 3 );
  builder.writeToDisk( inputPath );

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "rs:infer" );
  REQUIRE( op );

  Json::Value params( Json::objectValue );
  params["input"] = inputPath.toStdString();
  params["model"] = "ident";
  params["output"] = dir.filePath( QStringLiteral( "output.tif" ) ).toStdString();

  // Dynamic estimate reflects the real tile geometry (64 + 2*8 halo = 80).
  const Json::Value est = op->estimateExecution( params );
  REQUIRE( est.isObject() );
  CHECK( est["tileWidth"].asUInt64() == 80 );
  CHECK( est["tileHeight"].asUInt64() == 80 );
  CHECK( est["estimatedRamBytes"].asUInt64() > 0 );
  CHECK( est["basis"].asString() == "dynamic" );

  // The estimate reaches the ExecutionPlane's admission input in MiB.
  const unsigned int mib = sicnu::processing::ExecutionPlane::estimateFromPreflight( "rs:infer", params );
  CHECK( mib > 0 );

  // CPU-only manifest → no VRAM key in the estimate payload.
  CHECK_FALSE( est.isMember( "estimatedVramMb" ) );
}
