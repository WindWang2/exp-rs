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
#include <cstdint>
#include <memory>
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

TEST_CASE( "tile engine preserves GDAL band order for multi-band inputs (no R/B swap)", "[models][runtime][engine]" )
{
  REQUIRE( QFile::exists( identityModelPath() ) );

  QTemporaryDir dir;
  const QString inputPath = dir.filePath( QStringLiteral( "input.tif" ) );
  const QString outputPath = dir.filePath( QStringLiteral( "output.tif" ) );

  sicnu::testing::RsSyntheticRasterBuilder builder( 64, 64, 3 );
  // Distinct per-band constants so a channel swap is directly observable.
  builder.withConstantValue( 1, 10.0f );
  builder.withConstantValue( 2, 20.0f );
  builder.withConstantValue( 3, 30.0f );
  builder.writeToDisk( inputPath );

  ModelInfo info;
  info.name = "identity";
  info.framework = "onnx";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = identityModelPath().toStdString();
  info.tiling.tileSize = 64; // single tile

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  const auto session = registry.acquire( info );
  REQUIRE( session );

  RSOperatorContext context;
  TileInferenceEngine engine( info, session );
  const auto stats = engine.run( inputPath.toStdString(), {}, outputPath.toStdString(), context );
  CHECK( stats.outBands == 3 );

  // Identity must be per-band: output band i == input band i. OpenCV's
  // blobFromImage default swapRB=true would exchange bands 1 and 3.
  const float expected[3] = { 10.0f, 20.0f, 30.0f };
  GdalDatasetWrapper out;
  REQUIRE( out.open( outputPath ) );
  REQUIRE( out.bandCount() == 3 );
  for ( int b = 1; b <= 3; ++b )
  {
    std::vector<float> pixels( 64 * 64 );
    REQUIRE( out.readBandData( b, pixels.data(), 64, 64 ) );
    for ( std::size_t i = 0; i < pixels.size(); ++i )
      CHECK( pixels[i] == Catch::Approx( expected[b - 1] ).margin( 1e-3 ) );
  }
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

// ---------------------------------------------------------------------------
// Contract enforcement additions (#690 / #671 / #705)
// ---------------------------------------------------------------------------

TEST_CASE( "tile engine contract validators", "[models][runtime][engine]" )
{
  using sicnu::operators::runtime::TileInferenceEngine;

  ModelInfo info;

  // #705a: input.dtype is checked for EVERY consumed band, not just band 1.
  CHECK( TileInferenceEngine::inputDTypeMismatch( info, { 1, 2 }, []( int ) { return GDT_Float32; } ).empty() );
  info.input.dtype = "float32";
  CHECK( TileInferenceEngine::inputDTypeMismatch( info, { 1, 2, 3 }, []( int ) { return GDT_Float32; } ).empty() );
  const std::string mixedError = TileInferenceEngine::inputDTypeMismatch(
    info, { 1, 2, 3 }, []( int band ) { return band == 2 ? GDT_UInt16 : GDT_Float32; } );
  CHECK( mixedError.find( "band 2" ) != std::string::npos );
  CHECK( mixedError.find( "float32" ) != std::string::npos );
  // The fed band list (not the full raster) is the checked surface.
  CHECK( TileInferenceEngine::inputDTypeMismatch( info, { 3 }, []( int band ) {
          return band == 2 ? GDT_UInt16 : GDT_Float32;
        } ).empty() );
  info.input.dtype = "bfloat16";
  CHECK( TileInferenceEngine::inputDTypeMismatch( info, { 1 }, []( int ) { return GDT_Float32; } )
           .find( "unsupported input dtype" ) != std::string::npos );

  // #705d: declared tensor names must exist in the graph (when enumerable).
  CHECK( TileInferenceEngine::missingOutputTensor( info, {} ).empty() ); // nothing declared
  info.output.tensorNames = { "probability" };
  CHECK( TileInferenceEngine::missingOutputTensor( info, {} ).empty() ); // graph not enumerable
  CHECK( TileInferenceEngine::missingOutputTensor( info, { "logits", "probability" } ).empty() );
  const std::string missing = TileInferenceEngine::missingOutputTensor( info, { "logits" } );
  CHECK( missing.find( "probability" ) != std::string::npos );
  CHECK( missing.find( "logits" ) != std::string::npos );

  // #690: the writer emits float32 — other depths are rejected by name.
  CHECK( TileInferenceEngine::outputTypeMismatch( CV_32F, "probability" ).empty() );
  const std::string typeError = TileInferenceEngine::outputTypeMismatch( CV_32S, "probability" );
  CHECK( typeError.find( "probability" ) != std::string::npos );
  CHECK( typeError.find( "CV_32S" ) != std::string::npos );
  CHECK( typeError.find( "CV_32F" ) != std::string::npos );

  // #705d: declared classes count is a channel-count contract on the head.
  CHECK( TileInferenceEngine::classesChannelMismatch( info, 1, "probability" ).empty() );
  info.output.classes = { "background", "building" };
  CHECK( TileInferenceEngine::classesChannelMismatch( info, 2, "probability" ).empty() );
  const std::string classesError = TileInferenceEngine::classesChannelMismatch( info, 1, "probability" );
  CHECK( classesError.find( "probability" ) != std::string::npos );
  CHECK( classesError.find( "2 classes" ) != std::string::npos );

  // #705b: the whole-batch NoData skip decision.
  CHECK_FALSE( TileInferenceEngine::batchIsAllNoData( {} ) );
  CHECK( TileInferenceEngine::batchIsAllNoData( { 0, 0, 0 } ) );
  CHECK_FALSE( TileInferenceEngine::batchIsAllNoData( { 0, 4096, 0 } ) );
}

namespace {
/// Fake runtime for output-contract enforcement: reports fixed graph output
/// names, records the requested output tensor, and can serve a non-CV_32F
/// result to prove the engine rejects it (#690/#705).
class NamedOutputRuntime final : public IModelRuntime
{
  public:
    NamedOutputRuntime( std::vector<std::string> outputNames, int cvType = CV_32F )
        : m_outputNames( std::move( outputNames ) ), m_cvType( cvType ) {}

    std::string framework() const override { return "fakefw"; }
    std::string backendName() const override { return "fake_backend"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "/tmp/fake.onnx"; }

    std::vector<std::string> outputTensorNames() const override { return m_outputNames; }

    cv::Mat infer( const cv::Mat &nchwBlob ) override { return infer( nchwBlob, std::string() ); }

    cv::Mat infer( const cv::Mat &nchwBlob, const std::string &outputName ) override
    {
      std::lock_guard<std::mutex> lock( m_mutex );
      ++inferCount;
      lastRequestedOutput = outputName;
      cv::Mat out;
      if ( m_cvType == CV_32F )
        out = nchwBlob.clone();
      else
        nchwBlob.convertTo( out, m_cvType );
      return out;
    }

    int inferCount = 0;
    std::string lastRequestedOutput;

  private:
    std::vector<std::string> m_outputNames;
    int m_cvType;
    std::mutex m_mutex;
};

/// 64x64 single-band float32 raster on disk.
QString writeTinyRaster( const QTemporaryDir &dir, const QString &name )
{
  const QString path = dir.filePath( name );
  sicnu::testing::RsSyntheticRasterBuilder builder( 64, 64, 1 );
  builder.withRampPattern( 1, 0.0f, 50.0f );
  builder.writeToDisk( path );
  return path;
}

ModelInfo bareModel()
{
  ModelInfo info;
  info.name = "bare";
  info.framework = "fakefw";
  info.readiness = ModelReadiness::Ready;
  return info;
}
} // namespace

TEST_CASE( "tile engine runs the declared output tensor head", "[models][runtime][engine]" )
{
  QTemporaryDir dir;
  const QString inputPath = writeTinyRaster( dir, QStringLiteral( "input.tif" ) );
  const QString outputPath = dir.filePath( QStringLiteral( "output.tif" ) );

  auto runtime = std::make_shared<NamedOutputRuntime>(
    std::vector<std::string>{ "logits", "probability" } );

  ModelInfo info = bareModel();
  info.output.tensorNames = { "probability" };

  TileInferenceEngine engine( info, runtime );
  RSOperatorContext context;
  REQUIRE_NOTHROW( engine.run( inputPath.toStdString(), {}, outputPath.toStdString(), context ) );
  // The declared name selected the forward head.
  CHECK( runtime->inferCount == 1 );
  CHECK( runtime->lastRequestedOutput == "probability" );
}

TEST_CASE( "tile engine rejects a declared output tensor the graph lacks", "[models][runtime][engine]" )
{
  QTemporaryDir dir;
  const QString inputPath = writeTinyRaster( dir, QStringLiteral( "input.tif" ) );

  auto runtime = std::make_shared<NamedOutputRuntime>( std::vector<std::string>{ "logits" } );
  ModelInfo info = bareModel();
  info.output.tensorNames = { "probability" };

  TileInferenceEngine engine( info, runtime );
  RSOperatorContext context;
  bool threw = false;
  try
  {
    engine.run( inputPath.toStdString(), {}, dir.filePath( QStringLiteral( "unused.tif" ) ).toStdString(), context );
  }
  catch ( const RSOperatorError &e )
  {
    threw = true;
    const std::string what = e.what();
    CHECK( what.find( "probability" ) != std::string::npos );
    CHECK( what.find( "logits" ) != std::string::npos );
  }
  CHECK( threw );
  CHECK( runtime->inferCount == 0 ); // failed before any forward pass
}

TEST_CASE( "tile engine rejects class-count conflicts and non-float32 outputs", "[models][runtime][engine]" )
{
  QTemporaryDir dir;
  const QString inputPath = writeTinyRaster( dir, QStringLiteral( "input.tif" ) );

  // #705d: the identity head writes 1 channel; 2 declared classes conflict.
  {
    auto runtime = std::make_shared<NamedOutputRuntime>( std::vector<std::string>{} );
    ModelInfo info = bareModel();
    info.output.classes = { "background", "building" };
    TileInferenceEngine engine( info, runtime );
    RSOperatorContext context;
    bool threw = false;
    try
    {
      engine.run( inputPath.toStdString(), {},
                  dir.filePath( QStringLiteral( "classes.tif" ) ).toStdString(), context );
    }
    catch ( const RSOperatorError &e )
    {
      threw = true;
      CHECK( std::string( e.what() ).find( "2 classes" ) != std::string::npos );
    }
    CHECK( threw );
    CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "classes.tif" ) ) ) );
  }

  // #690: a CV_32S head must never be bit-cast into the float32 raster.
  {
    auto runtime = std::make_shared<NamedOutputRuntime>( std::vector<std::string>{}, CV_32S );
    ModelInfo info = bareModel();
    TileInferenceEngine engine( info, runtime );
    RSOperatorContext context;
    bool threw = false;
    try
    {
      engine.run( inputPath.toStdString(), {},
                  dir.filePath( QStringLiteral( "int32.tif" ) ).toStdString(), context );
    }
    catch ( const RSOperatorError &e )
    {
      threw = true;
      const std::string what = e.what();
      CHECK( what.find( "CV_32F" ) != std::string::npos );
      CHECK( what.find( "CV_32S" ) != std::string::npos );
    }
    CHECK( threw );
    CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "int32.tif" ) ) ) );
  }
}

TEST_CASE( "tile engine skips the forward pass for all-nodata tiles", "[models][runtime][engine]" )
{
  QTemporaryDir dir;
  const QString outputPath = dir.filePath( QStringLiteral( "output.tif" ) );

  // One of the four 64px core tiles is fully nodata: its forward pass must be
  // skipped and NoData written directly (#705).
  {
    const QString inputPath = dir.filePath( QStringLiteral( "partial.tif" ) );
    sicnu::testing::RsSyntheticRasterBuilder builder( 128, 128, 1 );
    builder.withNoData( -9999.0 ).withRampPattern( 1, 0.0f, 100.0f );
    for ( int y = 0; y < 64; ++y )
      for ( int x = 64; x < 128; ++x )
        builder.withPixel( 1, x, y, -9999.0f );
    builder.writeToDisk( inputPath );

    auto runtime = std::make_shared<FakeRuntime>( "/tmp/fake.onnx", 1.0f );
    ModelInfo info = bareModel();
    info.tiling.tileSize = 64;

    TileInferenceEngine engine( info, runtime );
    RSOperatorContext context;
    const auto stats = engine.run( inputPath.toStdString(), {}, outputPath.toStdString(), context );
    CHECK( stats.tilesProcessed == 4 );
    CHECK( stats.tilesSkippedNoData == 1 );
    CHECK( runtime->inferCount == 3 );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    std::vector<float> pixels( 128 * 128 );
    REQUIRE( out.readBandData( 1, pixels.data(), 128, 128 ) );
    int nanCount = 0;
    for ( int row = 0; row < 64; ++row )
      for ( int col = 64; col < 128; ++col )
        if ( std::isnan( pixels[static_cast<std::size_t>( row ) * 128 + col] ) )
          ++nanCount;
    CHECK( nanCount == 64 * 64 );
  }

  // Entire raster nodata: exactly one probe forward establishes the output
  // shape, everything else is written as NoData directly.
  {
    const QString inputPath = dir.filePath( QStringLiteral( "allnodata.tif" ) );
    sicnu::testing::RsSyntheticRasterBuilder builder( 128, 128, 1 );
    builder.withNoData( -9999.0 ).withConstantValue( 1, -9999.0f );
    builder.writeToDisk( inputPath );

    auto runtime = std::make_shared<FakeRuntime>( "/tmp/fake.onnx", 1.0f );
    ModelInfo info = bareModel();
    info.tiling.tileSize = 64;

    TileInferenceEngine engine( info, runtime );
    RSOperatorContext context;
    const auto stats = engine.run( inputPath.toStdString(), {}, outputPath.toStdString(), context );
    CHECK( stats.tilesProcessed == 4 );
    CHECK( stats.tilesSkippedNoData == 4 );
    CHECK( runtime->inferCount == 1 );
    CHECK( stats.outBands == 1 );

    GdalDatasetWrapper out;
    REQUIRE( out.open( outputPath ) );
    std::vector<float> pixels( 128 * 128 );
    REQUIRE( out.readBandData( 1, pixels.data(), 128, 128 ) );
    int nanCount = 0;
    for ( const float p : pixels )
      nanCount += std::isnan( p ) ? 1 : 0;
    CHECK( nanCount == 128 * 128 );
  }
}

TEST_CASE( "rs:infer floors the model RAM term with the artifact size", "[models][runtime][admission]" )
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
  params["output"] = dir.filePath( QStringLiteral( "output.tif" ) ).toStdString();

  // Shared window math: edge = 64 + 2*8 halo, 3 bands, 4 B/sample, 4 buffer
  // sets (batch 1). The manifest declares no estimated_ram_mb, so #689 floors
  // the model term with the artifact size (rounded up to whole MiB).
  const std::uint64_t windowBytes = 80ull * 80 * 3 * 4 * 4;
  const auto artifactMiB = [&] {
    const auto model = ModelCatalog::instance().find( "ident" );
    REQUIRE( model.has_value() );
    const qint64 bytes = QFileInfo( QString::fromStdString( model->resolvedArtifactPath ) ).size();
    REQUIRE( bytes > 0 );
    return ( static_cast<std::uint64_t>( bytes ) + 1024ull * 1024 - 1 ) / ( 1024ull * 1024 );
  }();

  {
    params["model"] = "ident";
    const Json::Value est = op->estimateExecution( params );
    REQUIRE( est.isMember( "estimatedRamBytes" ) );
    const std::uint64_t expected =
      windowBytes + artifactMiB * 1024ull * 1024 + 32ull * 1024 * 1024;
    CHECK( est["estimatedRamBytes"].asUInt64() == expected );
  }

  // An explicit runtime.estimated_ram_mb still wins over the floor.
  {
    QDir( dir.path() ).mkpath( QStringLiteral( "ident-ram" ) );
    QFile::copy( identityModelPath(), dir.filePath( QStringLiteral( "ident-ram/model.onnx" ) ) );
    QFile manifest( dir.filePath( QStringLiteral( "ident-ram/model.json" ) ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( R"({
        "name": "ident-ram",
        "task": "segmentation",
        "framework": "onnx",
        "artifact": { "path": "model.onnx" },
        "runtime": { "estimated_ram_mb": 768 }
    })" );
    manifest.close();
    ModelCatalog::instance().reload();

    params["model"] = "ident-ram";
    const Json::Value est = op->estimateExecution( params );
    REQUIRE( est.isMember( "estimatedRamBytes" ) );
    const std::uint64_t expected =
      windowBytes + 768ull * 1024 * 1024 + 32ull * 1024 * 1024;
    CHECK( est["estimatedRamBytes"].asUInt64() == expected );
  }
}
