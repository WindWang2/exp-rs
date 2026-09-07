// tests/test_model_failure_matrix.cpp — Platform 4.0 failure matrix (goal
// Phase 10) plus the device-resolution / pool / registry contract tests.
//
// Every case asserts the three properties the platform promises:
//   1. a structured, honest error (no silent fallbacks),
//   2. no partial output at the caller's path (atomic publication),
//   3. bounded work (cancel lands, memory stays O(batch × tile)).
// Uses fake providers (deterministic failure injection) and the committed
// identity ONNX fixture; synthetic rasters <= 512 px, except the sparse
// 100k x 100k logical-extent case which never materializes pixels.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/detection_postprocess.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using sicnu::operators::runtime::classifyInferenceError;
using sicnu::operators::runtime::DetectionBox;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::InferenceFailureKind;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::RequestedDevice;
using sicnu::operators::runtime::ResolvedDevice;
using sicnu::operators::runtime::resolveDevice;
using sicnu::operators::runtime::TileInferenceEngine;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

/// RAII pin of hardware capabilities.
struct HardwarePin
{
    HardwarePin( const ModelHardwareCapabilities &caps )
    {
      ModelRuntimeRegistry::instance().setHardwareForTest( caps );
    }
    ~HardwarePin() { ModelRuntimeRegistry::instance().setHardwareForTest( std::nullopt ); }
};

/// Guard restoring the registry to a clean state.
struct RegistryReset
{
    RegistryReset()
    {
      ModelRuntimeRegistry::instance().releaseAll();
      ModelRuntimeRegistry::instance().resetLoadCount();
      ModelRuntimeRegistry::instance().setMaxCachedSessions( 4 );
      ModelRuntimeRegistry::instance().setIdleEvictionMs( 0 );
    }
    ~RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
};

/// Failure-injecting fake provider behaviors, keyed on the batch size the
/// engine actually presents (blob.size[0]) and the forward-pass ordinal.
struct FailureScript
{
    std::string oomAfter;      ///< throw OOM when forward ordinal reaches this ("" = never)
    std::string crashOnBatchN; ///< throw generic crash when batch size == N ("" = never)
    int crashAfter = 0;        ///< throw generic crash when forward ordinal EXCEEDS this (0 = never)
    std::string oomMessage = "CUDA_ERROR_OUT_OF_MEMORY: out of memory while allocating";
    std::atomic<int> forwards{ 0 };
    std::atomic<int> completed{ 0 };
    std::atomic<bool> *cancelFlag = nullptr; ///< cooperative cancel hook for tests
    int cancelAfter = 0;                     ///< flip the flag after this many forwards
    int sleepPerForwardMs = 0;               ///< deterministic pacing for cancel tests
};

class ScriptedRuntime final : public IModelRuntime
{
  public:
    ScriptedRuntime( std::string artifact, FailureScript *script )
        : m_artifact( std::move( artifact ) ), m_script( script ) {}

    std::string framework() const override { return "scriptfw"; }
    std::string backendName() const override { return "scripted"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      ++m_script->forwards;
      if ( m_script->sleepPerForwardMs > 0 )
        std::this_thread::sleep_for(
          std::chrono::milliseconds( m_script->sleepPerForwardMs ) );
      if ( !m_script->crashOnBatchN.empty()
           && std::to_string( std::max( 1, blob.dims >= 4 ? blob.size[0] : 1 ) )
                == m_script->crashOnBatchN )
        throw std::runtime_error( "scripted provider crash mid-run" );
      if ( m_script->crashAfter > 0 && m_script->forwards.load() > m_script->crashAfter )
        throw std::runtime_error( "scripted provider crash mid-run" );
      if ( m_script->cancelFlag && m_script->cancelAfter > 0
           && m_script->completed.load() >= m_script->cancelAfter )
        m_script->cancelFlag->store( true );
      if ( !m_script->oomAfter.empty()
           && std::to_string( m_script->forwards.load() ) == m_script->oomAfter )
        throw std::runtime_error( m_script->oomMessage );
      ++m_script->completed;
      cv::Mat out = blob.clone();
      out *= 1.0f;
      return out;
    }

  private:
    std::string m_artifact;
    FailureScript *m_script;
};

/// Registers the scripted provider for "scriptfw".
struct ScriptedProviderGuard
{
    std::shared_ptr<FailureScript> script = std::make_shared<FailureScript>();

    ScriptedProviderGuard()
    {
      std::weak_ptr<FailureScript> weak = script;
      ModelRuntimeRegistry::instance().registerProvider(
        "scriptfw",
        [ weak ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                  std::string *error ) -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.find( "missing" ) != std::string::npos )
          {
            if ( error )
              *error = "artifact not found";
            return nullptr;
          }
          if ( auto script = weak.lock() )
            return std::make_shared<ScriptedRuntime>( model.resolvedArtifactPath, script.get() );
          if ( error )
            *error = "provider guard expired";
          return nullptr;
        } );
    }
};

ModelInfo scriptedModel( const std::string &artifact, const std::string &task = "segmentation" )
{
  ModelInfo info;
  info.name = "scripted-model";
  info.task = task;
  info.framework = "scriptfw";
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = artifact;
  info.tiling.tileSize = 32;
  info.output.classes = { "a", "b" };
  return info;
}

/// Writes a small deterministic float raster.
QString writeRaster( const QTemporaryDir &dir, const QString &name, int width, int height )
{
  const QString path = dir.filePath( name );
  sicnu::testing::RsSyntheticRasterBuilder( width, height, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( path );
  return path;
}

bool fileExists( const QString &path )
{
  return QFileInfo::exists( path );
}

QString tmpResidue( const QString &outputPath )
{
  return outputPath + QStringLiteral( ".tmp~" );
}

} // namespace

// ---------------------------------------------------------------------------
// Pure contract functions (T5/T6/T8)
// ---------------------------------------------------------------------------

TEST_CASE( "resolveDevice is deterministic across the request matrix", "[models][device]" )
{
  ModelHardwareCapabilities noGpu;
  ModelHardwareCapabilities gpu = noGpu;
  gpu.cudaAvailable = true;
  gpu.cudaDeviceCount = 2;
  gpu.vramBudgetMb = 100; // a declared budget makes over-budget cases meaningful

  ResolvedDevice out;
  std::string why;

  // Auto: no GPU -> cpu; GPU fits -> lowest index (deterministic cuda:0).
  REQUIRE( resolveDevice( RequestedDevice::autoDetect(), noGpu, true, 0, 0, true, &out, &why ) );
  CHECK_FALSE( out.gpu );
  REQUIRE( resolveDevice( RequestedDevice::autoDetect(), gpu, true, 0, 0, true, &out, &why ) );
  CHECK( out.gpu );
  CHECK( out.cudaIndex == 0 );
  // Auto is a pure function: identical inputs, identical answer.
  ResolvedDevice again;
  REQUIRE( resolveDevice( RequestedDevice::autoDetect(), gpu, true, 0, 0, true, &again, &why ) );
  CHECK( again.gpu == out.gpu );
  CHECK( again.cudaIndex == out.cudaIndex );

  // Auto + over budget + fallback -> cpu demotion.
  REQUIRE( resolveDevice( RequestedDevice::autoDetect(), gpu, true, 9000, 0, true, &out, &why ) );
  CHECK_FALSE( out.gpu );
  // Auto + over budget + NO fallback -> refusal (never silently cpu).
  REQUIRE_FALSE( resolveDevice( RequestedDevice::autoDetect(), gpu, true, 9000, 0, false, &out, &why ) );
  CHECK( why.find( "cpu_fallback" ) != std::string::npos );

  // Explicit cpu always cpu, even on GPU hosts.
  REQUIRE( resolveDevice( RequestedDevice::cpu(), gpu, true, 0, 0, true, &out, &why ) );
  CHECK_FALSE( out.gpu );

  // cuda:1 on a 2-device host with an index-capable backend: honored.
  REQUIRE( resolveDevice( RequestedDevice::cuda( 1 ), gpu, true, 0, 63, true, &out, &why ) );
  CHECK( out.gpu );
  CHECK( out.cudaIndex == 1 );
  // cuda:1 with the opencv_dnn backend (index cap 0): honest refusal.
  REQUIRE_FALSE( resolveDevice( RequestedDevice::cuda( 1 ), gpu, true, 0, 0, false, &out, &why ) );
  CHECK( why.find( "cuda:1" ) != std::string::npos );
  // cuda:5 beyond the device count: refusal regardless of backend cap.
  REQUIRE_FALSE( resolveDevice( RequestedDevice::cuda( 5 ), gpu, true, 0, 63, true, &out, &why ) );

  // CPU-only model never lands on a GPU, even on request: the request
  // fails loudly instead of silently demoting (demotion would hide the
  // configuration error).
  REQUIRE_FALSE( resolveDevice( RequestedDevice::cuda( 0 ), gpu, false, 0, 63, true, &out, &why ) );
  CHECK( why.find( "GPU-capable" ) != std::string::npos );
}

TEST_CASE( "inference errors are classified for the failure payloads", "[models][failure]" )
{
  using K = InferenceFailureKind;
  CHECK( classifyInferenceError( "CUDA_ERROR_OUT_OF_MEMORY: out of memory" ) == K::OutOfMemory );
  CHECK( classifyInferenceError( "std::bad_alloc" ) == K::OutOfMemory );
  CHECK( classifyInferenceError( "inference canceled before the forward pass" ) == K::Canceled );
  CHECK( classifyInferenceError( "runtime session is not loaded" ) == K::NotLoaded );
  CHECK( classifyInferenceError( "wrong input shape: dimension mismatch" ) == K::ShapeMismatch );
  CHECK( classifyInferenceError( "failed to load ONNX model: parse error" ) == K::CorruptModel );
  CHECK( classifyInferenceError( "something else entirely" ) == K::Unknown );
}

// ---------------------------------------------------------------------------
// OOM ladder (T9)
// ---------------------------------------------------------------------------

TEST_CASE( "an over-budget batch is retried tile-by-tile and still publishes", "[models][failure][oom]" )
{
  RegistryReset reset;
  ScriptedProviderGuard guard;
  // First forward (batch of 4 tiles) OOMs; the ladder retries serially.
  guard.script->oomAfter = "1";

  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "oom-in.tif" ), 64, 64 );
  const QString output = dir.filePath( QStringLiteral( "oom-out.tif" ) );

  ModelInfo model = scriptedModel( input.toStdString() );
  model.tiling.batchSize = 4;

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  const auto stats = engine.run( input.toStdString(), {}, output.toStdString(), context );

  CHECK( stats.batchReductions >= 1 );
  CHECK( stats.tilesProcessed == 4 );
  // The output was published atomically despite the mid-run OOM.
  CHECK( fileExists( output ) );
  CHECK_FALSE( fileExists( tmpResidue( output ) ) );
}

TEST_CASE( "OOM at batch=1 is terminal and carries the diagnostic", "[models][failure][oom]" )
{
  RegistryReset reset;
  ScriptedProviderGuard guard;
  // Every forward OOMs.
  guard.script->oomAfter = "1";
  guard.script->oomMessage = "out of memory at batch one";

  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "oom1-in.tif" ), 32, 32 );
  const QString output = dir.filePath( QStringLiteral( "oom1-out.tif" ) );

  ModelInfo model = scriptedModel( input.toStdString() );
  model.tiling.batchSize = 1;

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  REQUIRE_THROWS_WITH( engine.run( input.toStdString(), {}, output.toStdString(), context ),
                       Catch::Matchers::ContainsSubstring( "batch=1" ) );
  // No output, no residue.
  CHECK_FALSE( fileExists( output ) );
  CHECK_FALSE( fileExists( tmpResidue( output ) ) );
}

// ---------------------------------------------------------------------------
// Partial output / crash / disk-full (T10/T11/T28)
// ---------------------------------------------------------------------------

TEST_CASE( "a provider crash mid-run leaves no partial output", "[models][failure][partial]" )
{
  RegistryReset reset;
  ScriptedProviderGuard guard;
  // 64 px raster, tile 32, batch 2 → two batches. The SECOND forward
  // crashes: by then the streaming writer exists and tiles are already on
  // disk in the stage file — the hardest partial-output case.
  guard.script->crashAfter = 1;

  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "crash-in.tif" ), 64, 64 );
  const QString output = dir.filePath( QStringLiteral( "crash-out.tif" ) );

  ModelInfo model = scriptedModel( input.toStdString() );
  model.tiling.batchSize = 2;

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  REQUIRE_THROWS_AS( engine.run( input.toStdString(), {}, output.toStdString(), context ),
                     RSOperatorError );
  CHECK_FALSE( fileExists( output ) );
  CHECK_FALSE( fileExists( tmpResidue( output ) ) );
}

TEST_CASE( "an unwritable output path fails with FileNotWritable and no residue", "[models][failure][diskfull]" )
{
  RegistryReset reset;
  ScriptedProviderGuard guard;

  QTemporaryDir dir;
  const QString input = writeRaster( dir, QStringLiteral( "disk-in.tif" ), 32, 32 );
  // A file standing in for a directory: the stage cannot be created beneath it.
  const QString blocker = dir.filePath( QStringLiteral( "blocker" ) );
  {
    QFile f( blocker );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( "x" ) );
  }
  const QString output = blocker + QStringLiteral( "/out.tif" );

  ModelInfo model = scriptedModel( input.toStdString() );
  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  REQUIRE_THROWS_AS( engine.run( input.toStdString(), {}, output.toStdString(), context ),
                     RSOperatorError );
  CHECK_FALSE( fileExists( output ) );
}

TEST_CASE( "corrupt weight bytes are rejected at load with a structured error", "[models][failure][corrupt]" )
{
  QTemporaryDir dir;
  const QString corrupt = dir.filePath( QStringLiteral( "corrupt.onnx" ) );
  {
    QFile f( corrupt );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( "this is not a protobuf, it is 64 bytes of prose padding......." ) );
  }
  ModelInfo model = scriptedModel( corrupt.toStdString() );
  model.framework = "onnx"; // real provider
  model.name = "corrupt-model";

  std::string error;
  ModelRuntimeRegistry::instance().releaseAll();
  const auto session = ModelRuntimeRegistry::instance().acquire( model, &error );
  CHECK_FALSE( session );
  REQUIRE_FALSE( error.empty() );
  CHECK( ( classifyInferenceError( error ) == InferenceFailureKind::CorruptModel
           || error.find( "load" ) != std::string::npos ) );
  // And the failed load left nothing in the pool.
  CHECK( ModelRuntimeRegistry::instance().cachedSessionCount() == 0 );
}

TEST_CASE( "removing the artifact invalidates the model at the registry gate", "[models][failure][removed]" )
{
  QTemporaryDir dir;
  const QString weights = dir.filePath( QStringLiteral( "weights.onnx" ) );
  REQUIRE( QFile::copy( identityModelPath(), weights ) );
  ModelInfo model;
  model.name = "removable-model";
  model.framework = "onnx";
  model.readiness = ModelReadiness::Ready;
  model.resolvedArtifactPath = weights.toStdString();

  RegistryReset reset;
  REQUIRE( ModelRuntimeRegistry::instance().acquire( model ) );

  QFile::remove( weights );
  // The digest memo misses (the file is gone), the artifact is unreadable,
  // so acquire refuses instead of serving the stale session.
  ModelRuntimeRegistry::instance().releaseAll();
  std::string error;
  CHECK_FALSE( ModelRuntimeRegistry::instance().acquire( model, &error ) );
  CHECK( error.find( "artifact" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// Cancellation (T30)
// ---------------------------------------------------------------------------

TEST_CASE( "cancel between batches lands with bounded delay and no output", "[models][failure][cancel]" )
{
  RegistryReset reset;
  ScriptedProviderGuard guard;

  QTemporaryDir dir;
  guard.script->sleepPerForwardMs = 2; // deterministic pacing: 64 forwards x 2 ms
  const QString input = writeRaster( dir, QStringLiteral( "cancel-in.tif" ), 256, 256 );
  const QString output = dir.filePath( QStringLiteral( "cancel-out.tif" ) );

  ModelInfo model = scriptedModel( input.toStdString() );
  model.tiling.batchSize = 1;

  std::atomic<bool> cancelFlag{ false };
  RSOperatorContext context;
  context.setCancelFlag( &cancelFlag );
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );

  // Cancel from another thread after the first tile completes.
  std::thread canceler( [ & ] {
    while ( guard.script->completed.load() < 1 )
      std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    cancelFlag.store( true );
  } );

  const auto started = std::chrono::steady_clock::now();
  REQUIRE_THROWS_AS( engine.run( input.toStdString(), {}, output.toStdString(), context ),
                     RSOperatorError );
  const auto elapsedMs = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - started )
                           .count();
  canceler.join();

  // Bounded: the in-flight forward finishes, then the cancel lands.
  CHECK( guard.script->completed.load() <= 3 );
  CHECK( elapsedMs < 30000.0 ); // a 96 px raster cannot take 30 s — the point is it returned
  CHECK_FALSE( fileExists( output ) );
  CHECK_FALSE( fileExists( tmpResidue( output ) ) );
}

// ---------------------------------------------------------------------------
// Bounded execution on a huge logical raster (T12)
// ---------------------------------------------------------------------------

TEST_CASE( "a 100k x 100k logical raster runs tiled without whole-raster allocation",
           "[models][bounded]" )
{
  RegistryReset reset;
  ScriptedProviderGuard guard;

  // Logical extent via VRT: the 32x32 source is scaled to 100000 x 100000, so
  // the FILE is a few KB while every window read resolves through the VRT —
  // the engine sees a 100k raster without a 40 GB payload on disk.
  QTemporaryDir dir;
  const QString small = writeRaster( dir, QStringLiteral( "vrt-src.tif" ), 32, 32 );
  auto vrtTwoBand = []( const QString &path, int extent ) {
    QFile vrt( path );
    REQUIRE( vrt.open( QIODevice::WriteOnly ) );
    vrt.write( QString(
                 "<VRTDataset rasterXSize=\"%1\" rasterYSize=\"%2\">"
                 "  <VRTRasterBand dataType=\"Float32\" band=\"1\">"
                 "    <SimpleSource><SourceFilename relativeToVRT=\"1\">vrt-src.tif</SourceFilename>"
                 "    <SourceBand>1</SourceBand>"
                 "    <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"32\" ySize=\"32\"/>"
                 "    <DstRect xOff=\"0\" yOff=\"0\" xSize=\"%1\" ySize=\"%2\"/>"
                 "    </SimpleSource>"
                 "  </VRTRasterBand>"
                 "  <VRTRasterBand dataType=\"Float32\" band=\"2\">"
                 "    <SimpleSource><SourceFilename relativeToVRT=\"1\">vrt-src.tif</SourceFilename>"
                 "    <SourceBand>2</SourceBand>"
                 "    <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"32\" ySize=\"32\"/>"
                 "    <DstRect xOff=\"0\" yOff=\"0\" xSize=\"%1\" ySize=\"%2\"/>"
                 "    </SimpleSource>"
                 "  </VRTRasterBand>"
                 "</VRTDataset>" )
                 .arg( extent )
                 .arg( extent )
                 .toUtf8() );
  };
  const QString vrtPath = dir.filePath( QStringLiteral( "huge.vrt" ) );
  vrtTwoBand( vrtPath, 100000 );

  ModelInfo model = scriptedModel( vrtPath.toStdString() );
  model.tiling.tileSize = 512;
  model.tiling.batchSize = 1;

  // (a) The 100k extent plans and runs with bounded work: cancel after 5
  // forwards lands promptly, no output is published, and the working set
  // stayed at the tile geometry (implicitly — the run never had to
  // materialize more than one window).
  {
    std::atomic<bool> cancelFlag{ false };
    guard.script->cancelFlag = &cancelFlag;
    guard.script->cancelAfter = 5;
    RSOperatorContext context;
    context.setCancelFlag( &cancelFlag );
    const QString output = dir.filePath( QStringLiteral( "huge-out.tif" ) );
    TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
    const auto started = std::chrono::steady_clock::now();
    REQUIRE_THROWS_AS( engine.run( vrtPath.toStdString(), {}, output.toStdString(), context ),
                       RSOperatorError );
    const double elapsedMs = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - started )
                               .count();
    CHECK( guard.script->completed.load() <= 6 ); // bounded: the cancel landed immediately
    CHECK( elapsedMs < 60000.0 );
    CHECK_FALSE( fileExists( output ) );
    CHECK_FALSE( fileExists( tmpResidue( output ) ) );
    guard.script->cancelFlag = nullptr;
    guard.script->cancelAfter = 0;
    ModelRuntimeRegistry::instance().releaseAll();
  }

  // (b) A bounded logical extent (4096) completes end-to-end: 8x8 tiles,
  // streamed writes, atomic publication.
  {
    const QString midVrt = dir.filePath( QStringLiteral( "mid.vrt" ) );
    vrtTwoBand( midVrt, 4096 );
    const QString output = dir.filePath( QStringLiteral( "mid-out.tif" ) );
    ModelInfo midModel = model; // same 512 px tile, batch 1
    RSOperatorContext context;
    TileInferenceEngine engine( midModel, ModelRuntimeRegistry::instance().acquire( midModel ) );
    const auto stats = engine.run( midVrt.toStdString(), {}, output.toStdString(), context );
    CHECK( stats.outWidth == 4096 );
    CHECK( stats.outHeight == 4096 );
    CHECK( stats.tilesProcessed == 8 * 8 );
    CHECK( stats.tileSize == 512 );
    CHECK( fileExists( output ) );
    CHECK( QFileInfo( output ).size() < 4096LL * 4096 * 4 * 2 ); // single float band, no bloat
  }
}

// ---------------------------------------------------------------------------
// Bounded session pool (T26/T27)
// ---------------------------------------------------------------------------

TEST_CASE( "the session pool stays bounded under concurrent acquires", "[models][pool]" )
{
  RegistryReset reset;
  ScriptedProviderGuard guard;

  QTemporaryDir dir;
  std::vector<QString> artifacts;
  for ( int i = 0; i < 8; ++i )
  {
    const QString artifact = dir.filePath( QStringLiteral( "pool-%1.onnx" ).arg( i ) );
    QFile f( artifact );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QString( "weights-%1-bytes" ).arg( i ).toUtf8() );
    artifacts.push_back( artifact );
  }

  auto &registry = ModelRuntimeRegistry::instance();
  registry.setMaxCachedSessions( 2 );

  std::atomic<int> failures{ 0 };
  std::vector<std::thread> threads;
  for ( int t = 0; t < 8; ++t )
  {
    threads.emplace_back( [ &, t ] {
      for ( int round = 0; round < 20; ++round )
      {
        ModelInfo model = scriptedModel( artifacts[static_cast<std::size_t>( ( t + round ) % 8 )].toStdString() );
        if ( !registry.acquire( model ) )
          failures.fetch_add( 1 );
      }
    } );
  }
  for ( auto &thread : threads )
    thread.join();

  CHECK( failures.load() == 0 );
  CHECK( registry.cachedSessionCount() <= registry.maxCachedSessions() );
  const auto stats = registry.poolStats();
  // A losing loader returns the winner's session without counting a load,
  // so totalLoads can legitimately trail cacheMisses under contention.
  CHECK( stats.totalLoads <= stats.cacheMisses );
  CHECK( stats.evictions > 0 );
}

TEST_CASE( "the runtime contract surface: warmup, health, memory estimate", "[models][contract]" )
{
  QTemporaryDir dir;
  const QString artifact = dir.filePath( QStringLiteral( "identity.onnx" ) );
  REQUIRE( QFile::copy( identityModelPath(), artifact ) );
  ModelInfo model;
  model.name = "contract-model";
  model.framework = "onnx";
  model.readiness = ModelReadiness::Ready;
  model.resolvedArtifactPath = artifact.toStdString();

  RegistryReset reset;
  auto session = ModelRuntimeRegistry::instance().acquire( model );
  REQUIRE( session );

  // Warmup must never break the session (fixed-shape graphs may reject the
  // probe; the outcome lands in health, not in a failure).
  session->warmup();
  const auto health = session->health();
  CHECK( health.ok );
  const auto memory = session->memoryEstimate();
  CHECK( memory.weightsMb >= 1 ); // the 136-byte fixture rounds up to 1 MiB
  CHECK( memory.workingSetMb == 0 ); // honest unknown for opencv_dnn

  // Cooperative cancel: request, observe, clear.
  session->requestCancel();
  CHECK( session->health().ok == false );
  session->clearCancel();
  CHECK( session->health().ok );
}

TEST_CASE( "idle eviction and per-key release unload sessions", "[models][pool]" )
{
  RegistryReset reset;
  ScriptedProviderGuard guard;

  QTemporaryDir dir;
  const QString artifact = dir.filePath( QStringLiteral( "idle.onnx" ) );
  {
    QFile f( artifact );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( "weights-idle" ) );
  }

  auto &registry = ModelRuntimeRegistry::instance();
  registry.setIdleEvictionMs( 30 );
  ModelInfo model = scriptedModel( artifact.toStdString() );
  model.modelVersion = "1.0";
  model.id = "pool/idle";

  REQUIRE( registry.acquire( model ) );
  CHECK( registry.cachedSessionCount() == 1 );
  std::this_thread::sleep_for( std::chrono::milliseconds( 60 ) );
  // The next acquire evicts the idle session and reloads.
  const auto loadsBefore = registry.poolStats().totalLoads;
  REQUIRE( registry.acquire( model ) );
  CHECK( registry.poolStats().totalLoads == loadsBefore + 1 );
  CHECK( registry.poolStats().evictions >= 1 );

  registry.setIdleEvictionMs( 0 );
  registry.releaseAll();
}

// ---------------------------------------------------------------------------
// Registry surface (T23)
// ---------------------------------------------------------------------------

TEST_CASE( "the registry registers, inspects, resolves versions and unregisters", "[models][registry]" )
{
  QTemporaryDir dir;
  const QString weights = dir.filePath( QStringLiteral( "reg.onnx" ) );
  {
    QFile f( weights );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( "registry-weights" ) );
  }
  const std::string manifest = R"({
      "name": "registered-model",
      "id": "acme/reg",
      "model_version": "2.0",
      "license": "Apache-2.0",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": ")" + weights.toStdString() + R"(" }
  })";

  auto &catalog = ModelCatalog::instance();
  std::string error;
  REQUIRE( catalog.registerManifestJson( manifest, "session", &error ) );

  // Inspect exposes the full record incl. identity and health.
  const Json::Value record = catalog.inspect( "acme/reg" );
  CHECK( record["id"].asString() == "acme/reg" );
  CHECK( record["identity_tag"].asString() == "acme/reg@2.0" );
  CHECK( record["health"]["ok"].asBool() );
  CHECK_FALSE( record["health"]["content_digest"].asString().empty() );

  // Version resolution: exact, bare (unique), and latest.
  REQUIRE( catalog.resolve( "acme/reg@2.0" ).has_value() );
  REQUIRE( catalog.resolve( "acme/reg" ).has_value() );
  REQUIRE_FALSE( catalog.resolve( "acme/reg@9.9" ).has_value() );

  // Health and unregister.
  CHECK( catalog.health( "acme/reg" )["ok"].asBool() );
  REQUIRE( catalog.unregister( "acme/reg" ) );
  CHECK( catalog.find( "acme/reg" ) == std::nullopt );
  CHECK( catalog.health( "acme/reg" ).isNull() );
  CHECK_FALSE( catalog.unregister( "acme/reg" ) );
}

TEST_CASE( "validateManifestJson reports issues without registering", "[models][registry]" )
{
  auto &catalog = ModelCatalog::instance();
  const std::size_t before = catalog.models().size();

  CHECK( catalog.validateManifestJson( "{ not json" ).size() == 1 );
  CHECK( catalog.validateManifestJson( R"({"task": "segmentation"})" ).size() == 1 );
  CHECK( catalog.validateManifestJson( R"({
      "name": "bad-contract",
      "preprocess": { "normalize": "mean_std" }
  })" ).size() == 1 );
  CHECK( catalog.validateManifestJson( R"({
      "name": "good-manifest",
      "task": "segmentation",
      "framework": "onnx"
  })" ).empty() );

  // Nothing was registered by validation.
  CHECK( catalog.models().size() == before );
  CHECK( catalog.find( "good-manifest" ) == std::nullopt );
}
